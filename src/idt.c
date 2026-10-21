/* Interrupt descriptor table, 8259 PIC, and the central interrupt dispatcher. */
#include "kernel.h"

struct idt_entry {
    u16 base_lo, sel;
    u8 zero, flags;
    u16 base_hi;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u32 base;
} __attribute__((packed));

static struct idt_entry idt[256];
static irq_handler_t irq_handlers[16];

extern u32 isr_stub_table[48];
extern void isr128(void);

volatile int need_resched;
int scheduler_running;

static const char *exception_names[32] = {
    "Divide error", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range exceeded",
    "Invalid opcode", "Device not available", "Double fault", "Coprocessor segment overrun",
    "Invalid TSS", "Segment not present", "Stack-segment fault", "General protection fault",
    "Page fault", "Reserved", "x87 floating-point error", "Alignment check", "Machine check",
    "SIMD floating-point error", "Virtualization error", "Control protection error",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor injection", "VMM communication", "Security exception", "Reserved",
};

static void idt_set(int n, u32 handler, u8 flags)
{
    idt[n].base_lo = handler & 0xFFFF;
    idt[n].base_hi = handler >> 16;
    idt[n].sel = KERNEL_CS;
    idt[n].zero = 0;
    idt[n].flags = flags;
}

/* Move IRQs 0-15 to vectors 32-47 so they don't collide with CPU exceptions. */
static void pic_remap(void)
{
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xFB); /* everything masked except the cascade line */
    outb(0xA1, 0xFF);
}

static void pic_eoi(int irq)
{
    if (irq >= 8)
        outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void irq_install(int irq, irq_handler_t handler)
{
    irq_handlers[irq] = handler;
    u16 port = irq < 8 ? 0x21 : 0xA1;
    outb(port, inb(port) & ~(1 << (irq & 7)));
}

void idt_init(void)
{
    for (int i = 0; i < 48; i++)
        idt_set(i, isr_stub_table[i], 0x8E);  /* present, ring 0, interrupt gate */
    idt_set(0x80, (u32)isr128, 0xEE);         /* present, ring 3 may call it     */

    struct idt_ptr ip = { sizeof idt - 1, (u32)idt };
    __asm__ volatile("lidt %0" :: "m"(ip));
    pic_remap();
}

static void exception_handler(struct regs *r)
{
    u32 cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    const char *name = exception_names[r->int_no];

    /* A fault in user mode kills the process, not the machine. */
    if ((r->cs & 3) == 3) {
        console_set_color(LIGHT_RED);
        kprintf("\n%s (pid %d) killed: %s at eip=%08x", current->name, current->pid, name, r->eip);
        if (r->int_no == 14)
            kprintf(" (%s %s address %08x)", (r->err_code & 2) ? "write to" : "read from",
                    (r->err_code & 1) ? "kernel-only" : "unmapped", cr2);
        kputs("\n");
        console_set_color(LIGHT_GREY);
        task_exit_current(139);
        return;
    }

    kpanic("%s (vector %u, error code %08x)\n"
           "  eip=%08x cs=%04x eflags=%08x cr2=%08x\n"
           "  eax=%08x ebx=%08x ecx=%08x edx=%08x\n"
           "  esi=%08x edi=%08x ebp=%08x",
           name, r->int_no, r->err_code, r->eip, r->cs, r->eflags, cr2,
           r->eax, r->ebx, r->ecx, r->edx, r->esi, r->edi, r->ebp);
}

/* Called from isr_common. The returned frame is the one that gets resumed. */
struct regs *interrupt_dispatch(struct regs *r)
{
    if (r->int_no < 32) {
        exception_handler(r);
    } else if (r->int_no < 48) {
        int irq = r->int_no - 32;
        pic_eoi(irq);
        if (irq_handlers[irq])
            irq_handlers[irq](r);
    } else if (r->int_no == 0x80) {
        syscall_handler(r);
    }

    if (need_resched && scheduler_running) {
        need_resched = 0;
        r = schedule(r);
    }
    return r;
}
