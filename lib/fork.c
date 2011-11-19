// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

extern void _pgfault_upcall(void);

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int errno;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).
	if (!(err & FEC_WR))
		panic("(pgfault) fault is not a write: err %08x\n", err);

	if (!(vpt[PGNUM(addr)] & PTE_COW))
		panic("(pgfault) fault on a non-cow page: va %p\n", addr);

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.
	errno = sys_page_alloc(0, PFTEMP, PTE_U | PTE_W);
	if (errno < 0)
		panic("user pgfault handler, failed allocating page: %e", errno);

	memmove(PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);

	errno = sys_page_map(0, PFTEMP, 0, ROUNDDOWN(addr, PGSIZE), PTE_U | PTE_W);
	if (errno < 0)
		panic("user pgfault handler, failed mapping new page: %e", errno);

	errno = sys_page_unmap(0, PFTEMP);
	if (errno < 0)
		panic("user pgfault handler, failed unmapping: %e\n", errno);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int errno;
	uintptr_t addr = pn * PGSIZE;
	int perm = PTE_U;

	if (vpt[pn] & (PTE_W | PTE_COW))
		perm |= PTE_COW;

	// map the page in the target env
	errno = sys_page_map(0, (void*)addr, envid, (void*)addr, perm);
	if (errno < 0)
	 	panic("(duppage) %e", errno);
	
	/* re-map the page in ourselves if needed */
	if (perm & PTE_COW) {
		errno = sys_page_map(0, (void*)addr, 0, (void*)addr, perm);
		if (errno < 0)
			panic("(duppage) %e", errno);
	}

	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use vpd, vpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	int errno;
	uintptr_t addr;
	envid_t childid;
	
	set_pgfault_handler(pgfault);
	
	childid = sys_exofork();
	if (childid < 0)
		panic("exofork failed: %e", childid);
	if (childid == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// copying address space
	for (addr = 0; addr < USTACKTOP; addr += PGSIZE) {
		if ((vpd[PDX(addr)] & PTE_P) == 0 ||
		    (vpt[PGNUM(addr)] & PTE_P) == 0)
			continue;
		
		duppage(childid, PGNUM(addr));
	}

	// allocate user exception stack for child
	errno = sys_page_alloc(childid, (void*)(UXSTACKTOP - PGSIZE), PTE_U | PTE_W);
	if (errno < 0)
		panic("page allocation for child exception stack failed: %e", errno);
	errno = sys_env_set_pgfault_upcall(childid, _pgfault_upcall);
	if (errno < 0)
		panic("setting pgfault upcall failed: %e", errno);

	sys_env_set_status(childid, ENV_RUNNABLE);
	return childid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
