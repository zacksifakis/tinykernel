#include "kernel.h"

#define MULTIBOOT_MAGIC 0x2BADB002

static void ok(const char *what)
{
    kputs("  [");
    console_set_color(LIGHT_GREEN);
    kputs(" OK ");
    console_set_color(LIGHT_GREY);
    kprintf("] %s\n", what);
}

void kmain(u32 magic, struct multiboot_info *mbi)
{
    console_init();
    console_set_color(LIGHT_CYAN);
    kputs("  _   _             _                         _\n"
          " | |_(_)_ __  _   _| | _____ _ __ _ __   ___| |\n"
          " | __| | '_ \\| | | | |/ / _ \\ '__| '_ \\ / _ \\ |\n"
          " | |_| | | | | |_| |   <  __/ |  | | | |  __/ |\n"
          "  \\__|_|_| |_|\\__, |_|\\_\\___|_|  |_| |_|\\___|_|\n"
          "              |___/        a tiny x86 kernel\n\n");
    console_set_color(LIGHT_GREY);

    if (magic != MULTIBOOT_MAGIC)
        kpanic("not loaded by a Multiboot bootloader (magic %08x)", magic);

    gdt_init();
    ok("GDT and TSS (ring 0 / ring 3 segments)");
    idt_init();
    ok("IDT, CPU exceptions, 8259 PIC remapped");
    mm_init(mbi);
    struct mm_stats s;
    mm_get_stats(&s);
    kprintf("  [");
    console_set_color(LIGHT_GREEN);
    kputs(" OK ");
    console_set_color(LIGHT_GREY);
    kprintf("] Paging on, %u MB RAM, %u KB kernel, 4 MB heap\n", s.total_pages / 256, s.kernel_size / 1024);
    tasks_init();
    ok("Scheduler (preemptive round robin)");
    timer_init(TIMER_HZ);
    ok("PIT timer at 100 Hz");
    input_init();
    ok("Keyboard and serial input");
    ok("System calls on int 0x80");

    task_spawn_kernel("shell", shell_main, NULL);
    scheduler_running = 1;
    __asm__ volatile("sti");

    /* From here on this is the idle task. */
    for (;;)
        __asm__ volatile("hlt");
}
