/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.text

TRAPHANDLER_NOEC(routine_divide, T_DIVIDE)
TRAPHANDLER_NOEC(routine_debug, T_DEBUG)
TRAPHANDLER_NOEC(routine_nmi, T_NMI)
TRAPHANDLER_NOEC(routine_brkpt, T_BRKPT)
TRAPHANDLER_NOEC(routine_oflow, T_OFLOW)
TRAPHANDLER_NOEC(routine_bound, T_BOUND)
TRAPHANDLER_NOEC(routine_illop, T_ILLOP)
TRAPHANDLER_NOEC(routine_device, T_DEVICE)
TRAPHANDLER(routine_dblflt, T_DBLFLT)
TRAPHANDLER(routine_tss, T_TSS)
TRAPHANDLER(routine_segnp, T_SEGNP)
TRAPHANDLER(routine_stack, T_STACK)
TRAPHANDLER(routine_gpflt, T_GPFLT)
TRAPHANDLER(routine_pgflt, T_PGFLT)
TRAPHANDLER_NOEC(routine_fperr, T_FPERR)
TRAPHANDLER(routine_align, T_ALIGN)
TRAPHANDLER_NOEC(routine_mchk, T_MCHK)
TRAPHANDLER_NOEC(routine_simderr, T_SIMDERR)

TRAPHANDLER_NOEC(routine_syscall, T_SYSCALL)

TRAPHANDLER_NOEC(irq0_handler ,IRQ_OFFSET)   
TRAPHANDLER_NOEC(irq1_handler ,IRQ_OFFSET+1);
TRAPHANDLER_NOEC(irq2_handler ,IRQ_OFFSET+2);
TRAPHANDLER_NOEC(irq3_handler ,IRQ_OFFSET+3);
TRAPHANDLER_NOEC(irq5_handler ,IRQ_OFFSET+4);
TRAPHANDLER_NOEC(irq4_handler ,IRQ_OFFSET+5);
TRAPHANDLER_NOEC(irq6_handler ,IRQ_OFFSET+6);
TRAPHANDLER_NOEC(irq7_handler ,IRQ_OFFSET+7);
TRAPHANDLER_NOEC(irq8_handler ,IRQ_OFFSET+8);
TRAPHANDLER_NOEC(irq9_handler ,IRQ_OFFSET+9);
TRAPHANDLER_NOEC(irq10_handler,IRQ_OFFSET+10);
TRAPHANDLER_NOEC(irq11_handler,IRQ_OFFSET+11);
TRAPHANDLER_NOEC(irq12_handler,IRQ_OFFSET+12);
TRAPHANDLER_NOEC(irq13_handler,IRQ_OFFSET+13);
TRAPHANDLER_NOEC(irq14_handler,IRQ_OFFSET+14);
TRAPHANDLER_NOEC(irq15_handler,IRQ_OFFSET+15);
	
_alltraps:
	push %ds
	push %es
	pushal 		#push struct Trapframe on stack
	movl $GD_KD, %eax
	movw %ax, %ds
	movw %ax, %es
	movl $0x0, %ebp      	# not sure if neccessary... TODO	
	pushl %esp
	call trap

