/*
 * Flat segmentation: kernel and user code/data segments covering all 4 GB,
 * plus a TSS so the CPU knows which stack to use when ring 3 traps into ring 0.
 */
#include "kernel.h"

struct gdt_entry {
    u16 limit_lo, base_lo;
    u8 base_mid, access, gran, base_hi;
} __attribute__((packed));

struct gdt_ptr {
    u16 limit;
    u32 base;
} __attribute__((packed));

struct tss {
    u32 prev, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags;
    u32 eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u32 es, cs, ss, ds, fs, gs, ldt;
    u16 trap, iomap_base;
} __attribute__((packed));

static struct gdt_entry gdt[6];
static struct gdt_ptr gp;
static struct tss tss;

extern void gdt_flush(struct gdt_ptr *p);

static void gdt_set(int i, u32 base, u32 limit, u8 access, u8 flags)
{
    gdt[i].base_lo = base & 0xFFFF;
    gdt[i].base_mid = (base >> 16) & 0xFF;
    gdt[i].base_hi = (base >> 24) & 0xFF;
    gdt[i].limit_lo = limit & 0xFFFF;
    gdt[i].gran = ((limit >> 16) & 0x0F) | (flags << 4);
    gdt[i].access = access;
}

void gdt_init(void)
{
    gdt_set(0, 0, 0, 0, 0);                /* null                  */
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xC);     /* 0x08 kernel code      */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xC);     /* 0x10 kernel data      */
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xC);     /* 0x18 user code (DPL3) */
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xC);     /* 0x20 user data (DPL3) */

    memset(&tss, 0, sizeof tss);
    tss.ss0 = KERNEL_DS;
    tss.iomap_base = sizeof tss;           /* no I/O bitmap: ring 3 gets no ports */
    gdt_set(5, (u32)&tss, sizeof tss - 1, 0x89, 0x0); /* 0x28 TSS */

    gp.limit = sizeof gdt - 1;
    gp.base = (u32)gdt;
    gdt_flush(&gp);
}

void tss_set_kernel_stack(u32 esp0)
{
    tss.esp0 = esp0;
}
