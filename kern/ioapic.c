// The I/O APIC manages hardware interrupts for an SMP system.
// http://www.intel.com/design/chipsets/datashts/29056601.pdf
// See also picirq.c.

#include <kern/ioapic.h>
#include <kern/mpconfig.h>

#include <inc/types.h>
#include <inc/trap.h>
#include <inc/stdio.h>

// IO APIC MMIO structure: write reg, then read or write data.
struct ioapic {
	uint32_t reg;
	uint32_t pad[3];
	uint32_t data;
};

static uint32_t
ioapicread(int reg)
{
	ioapic->reg = reg;
	return ioapic->data;
}

static void
ioapicwrite(int reg, uint32_t data)
{
	ioapic->reg = reg;
	ioapic->data = data;
}

void
ioapicinit(void)
{
	int i, id, maxintr;

	if(!ismp)
		return;

	assert(ioapicaddr != 0);
	cprintf("ioapic addr = %x\n", ioapicaddr);

	// Using ioapicaddr instead of the IOAPIC default address.
	ioapic = (volatile struct ioapic*)ioapicaddr;
	maxintr = (ioapicread(REG_VER) >> 16) & 0xFF;
	id = ioapicread(REG_ID) >> 24;
	// TODO
	// The IOAPIC we find always has the id that is equals to the amount of CPUS.
	if(id != ioapicid)
		cprintf("ioapicinit: id (%d) isn't equal to ioapicid (%d); not a MP\n",
			id, ioapicid);

	// Mark all interrupts edge-triggered, active high, disabled,
	// and not routed to any CPUs.
	for(i = 0; i <= maxintr; i++) {
		ioapicwrite(REG_TABLE+2*i, INT_DISABLED | (IRQ_OFFSET + i));
		ioapicwrite(REG_TABLE+2*i+1, 0);
	}
}

void
ioapicenable(int irq, int cpunum)
{
	if(!ismp)
		return;

	// Mark interrupt edge-triggered, active high,
	// enabled, and routed to the given cpunum,
	// which happens to be that cpu's APIC ID.
	ioapicwrite(REG_TABLE+2*irq, IRQ_OFFSET + irq);
	ioapicwrite(REG_TABLE+2*irq+1, cpunum << 24);
}

// TODO
// what is this shit for
void
ioapicdisable(int irq, int cpunum)
{
	if(!ismp)
		return;
  
	ioapicwrite(REG_TABLE+2*irq, INT_DISABLED | (IRQ_OFFSET + irq));
	ioapicwrite(REG_TABLE+2*irq+1, cpunum << 24);
}
