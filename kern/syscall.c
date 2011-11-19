/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.
	user_mem_assert(curenv, (void*)s, len, PTE_U | PTE_P);

	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	if (e == curenv)
		cprintf("[%08x] exiting gracefully\n", curenv->env_id);
	else
		cprintf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.
	int errno;
	struct Env* newenv;

	errno = env_alloc(&newenv, curenv->env_id);
	if (errno < 0)
		return errno;

	newenv->env_status = ENV_NOT_RUNNABLE;
	newenv->env_tf = curenv->env_tf;
	newenv->env_tf.tf_regs.reg_eax = 0;

	return newenv->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.
	int errno;
	struct Env* target_env;
	
	errno = envid2env(envid, &target_env, 1);
	if (errno < 0)
		return errno;

	if (status != ENV_RUNNABLE &&
	    status != ENV_NOT_RUNNABLE)
		return -E_INVAL;

	target_env->env_status = status;
	return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	int errno;
	struct Env* target_env;

	errno = envid2env(envid, &target_env, 1);
	if (errno < 0)
		return errno;

	user_mem_assert(target_env, func, sizeof(uintptr_t), PTE_U);
	target_env->env_pgfault_upcall = func;

	return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!
	int errno;
	struct Env* target_env;
	struct Page* newpage;

	if ((~PTE_SYSCALL & perm) != 0)
		return -E_INVAL;
	// we don't check for PTE_P coz we cool like that
	if ((perm & PTE_U) == 0)
		return -E_INVAL;
	
	if ((uintptr_t)va >= UTOP)
		return -E_INVAL;
	if (ROUNDDOWN(va, PGSIZE) != va)
		return -E_INVAL;

	errno = envid2env(envid, &target_env, 1);
	if (errno < 0)
		return errno;

	newpage = page_alloc(ALLOC_ZERO);
	errno = page_insert(target_env->env_pgdir, newpage, va, perm);
	if (errno < 0) {
		page_free(newpage);
		return errno;
	}

	return 0;
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.
	int errno;
	struct Page* target_page;
	struct Env* srcenv;
	struct Env* dstenv;
	pte_t* src_pte;

	if ((uintptr_t)srcva >= UTOP || (uintptr_t)dstva >= UTOP ||
	    (ROUNDDOWN(srcva, PGSIZE)) != srcva ||
	    (ROUNDDOWN(dstva, PGSIZE)) != dstva)
		return -E_INVAL;
	
	if ((~PTE_SYSCALL & perm) != 0)
		return -E_INVAL;
	// we don't check for PTE_P coz we cool like that
	if ((perm & PTE_U) == 0)
		return -E_INVAL;
	
	errno = envid2env(srcenvid, &srcenv, 1);
	if (errno < 0)
		return errno;
	errno = envid2env(dstenvid, &dstenv, 1);
	if (errno < 0)
		return errno;

	target_page = page_lookup(srcenv->env_pgdir, srcva, &src_pte);
	if (target_page == NULL)
		return -E_INVAL;
	if ((*src_pte & PTE_W) == 0 && (perm & PTE_W) != 0)
		return -E_INVAL;

	errno = page_insert(dstenv->env_pgdir, target_page, dstva, perm);
	if (errno < 0)
		return errno;

	return 0;
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().
	int errno;
	struct Env* target_env;
	
	if ((uintptr_t)va >= UTOP || ROUNDDOWN(va, PGSIZE) != va)
		return -E_INVAL;

	errno = envid2env(envid, &target_env, 1);
	if (errno < 0)
		return errno;

	page_remove(target_env->env_pgdir, va);
	return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	int errno;
	struct Env* target_env;
	struct Page* sentpage;
	pte_t* sentpage_pte;

	errno = envid2env(envid, &target_env, 0);	// checkperm = 0
	if (errno < 0)
		return errno;

	if (target_env->env_ipc_recving == 0)
		return -E_IPC_NOT_RECV;
	assert(target_env->env_status == ENV_NOT_RUNNABLE);

	if (! ((uintptr_t)srcva < UTOP) ) {
		perm = 0;
		goto set_value_ontarget;
	}

	// from now on, srcva < UTOP is true
	
	// first check if target env wants to recieve a page, if not then don't
	// check anything related to permissions/mappings
	if (! ((uintptr_t)target_env->env_ipc_dstva < UTOP) ) {
		perm = 0;
		goto set_value_ontarget;
	}
	
	if (ROUNDDOWN(srcva, PGSIZE) != srcva)
		return -E_INVAL;
	// not checking for PTE_P, see sys_page_alloc for why
	if ((perm & PTE_U) == 0 || (~PTE_SYSCALL & perm) != 0)
		return -E_INVAL;

	sentpage = page_lookup(curenv->env_pgdir, srcva, &sentpage_pte);
	if (sentpage == NULL)
		return -E_INVAL;
	if ((perm & PTE_W) != 0 && (*sentpage_pte & PTE_W) == 0)
		return -E_INVAL;

	errno = page_insert(target_env->env_pgdir, sentpage,
			    target_env->env_ipc_dstva, perm);
	if (errno < 0)
		return errno;

set_value_ontarget:	
	target_env->env_ipc_recving	= 0;
	target_env->env_ipc_from	= curenv->env_id;
	target_env->env_ipc_value	= value;
	target_env->env_ipc_perm	= perm;

	target_env->env_status		= ENV_RUNNABLE;

	return errno;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	if ((uintptr_t)dstva < UTOP && ROUNDDOWN(dstva, PGSIZE) != dstva)
		return -E_INVAL;
	
	curenv->env_ipc_recving		= 1;
	curenv->env_ipc_dstva		= dstva;
	curenv->env_status		= ENV_NOT_RUNNABLE;
	curenv->env_tf.tf_regs.reg_eax	= 0;	// setting return value from
						// this system call

	sched_yield();

	// We never get here, and here's why: when we yield we wait for someone
	// to send us some value through IPC mechanism. That env also changes
	// our status to RUNNABLE. Then, when the scheduler chooses to run us it
	// calls env_run with our trapframe which contains the eip of user-space
	// program, not eip of <THIS> function.
	panic("sys_ipc_recv - how tf did we get here");
	
	return 0;
}

static int
sys_get_cpuid(void)
{
	return cpunum();
}


// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	int errno = 0;

	switch(syscallno) {
	case SYS_cputs:
		sys_cputs((const char*)a1, (size_t)a2);
		break;
	case SYS_cgetc:
		errno = sys_cgetc();
		break;
	case SYS_getenvid:
		errno = sys_getenvid();
		break;
	case SYS_env_destroy:
		errno = sys_env_destroy((envid_t)a1);
		break;
	case SYS_yield:
		sys_yield();
		break;
	case SYS_exofork:
		errno = sys_exofork();
		break;
	case SYS_env_set_status:
		errno = sys_env_set_status((envid_t)a1, (int)a2);
		break;
	case SYS_page_alloc:
		errno = sys_page_alloc((envid_t)a1, (void*)a2, (int)a3);
		break;
	case SYS_page_map:
		errno = sys_page_map((envid_t)a1, (void*)a2,
				     (envid_t)a3, (void*)a4, (int)a5);
		break;
	case SYS_page_unmap:
		errno = sys_page_unmap((envid_t)a1, (void*)a2);
		break;
	case SYS_env_set_pgfault_upcall:
		errno = sys_env_set_pgfault_upcall((envid_t)a1, (void*)a2);
		break;
	case SYS_ipc_try_send:
		errno = sys_ipc_try_send((envid_t)a1, (uint32_t)a2, (void*)a3,
					 (unsigned int)a4);
		break;
	case SYS_ipc_recv:
		errno = sys_ipc_recv((void*)a1);
		break;
	/* case SYS_env_set_trapframe: */
	/* 	errno = sys_env_set_trapframe((envid_t)a1, (struct Trapframe*)a2); */
	/* 	break; */

	// TODO - this syscall is redundant at the moment (users can read envs array)
	case SYS_get_cpuid:
		errno = sys_get_cpuid();
		break;
			
	default:
		errno = -E_INVAL;
	}

	return errno;
}

