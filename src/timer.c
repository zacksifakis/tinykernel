/* The 8253/8254 PIT drives the scheduler tick. */
#include "kernel.h"

volatile u32 ticks;

static void timer_irq(struct regs *r)
{
    (void)r;
    ticks++;
    current->cpu_ticks++;
    need_resched = 1; /* preempt: every tick is a scheduling decision */
}

void timer_init(u32 hz)
{
    u32 divisor = 1193182 / hz;
    outb(0x43, 0x36); /* channel 0, lo/hi byte, square wave */
    outb(0x40, divisor & 0xFF);
    outb(0x40, divisor >> 8);
    irq_install(0, timer_irq);
}
