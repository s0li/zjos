#include <inc/stdio.h>
#include <inc/trap.h>
#include <inc/lib.h>

#define TIME	(sys_time_msec() / 1000)

//int		sys_interrupt_redirect(uint32_t vector, uint32_t cpunum);
void umain(int argc, char **argv) {
	int i = 0;
	int errno;
	int c;

	cprintf("testing with invalid vector num:\n");
	errno = sys_intr_redirect(-1, 0);
	if (!(errno < 0))
		panic("failed test (invalid vector num)");
	errno = sys_intr_redirect(IRQ_TIMER, 0);
	if (!(errno < 0))
		panic("failed test (invalid vector num)");

	cprintf("starting test loop, to quit press 'q'. every keystroke should be recorded on a different cpu\n");
	while ((c = getchar()) != 'q') {
		errno = sys_intr_redirect(IRQ_KBD, i++);
		if (errno < 0) {
			cprintf("invalid cpunum passed to intr_redirect, changing to 0\n");
			i = 0;
		}
	}
}
