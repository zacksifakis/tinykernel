#ifndef KERNEL_H
#define KERNEL_H

#include "abi.h"

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef int                i32;
typedef unsigned int       size_t;
typedef int                bool;
#define true  1
#define false 0
#define NULL  ((void *)0)

typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, t)   __builtin_va_arg(v, t)

#define PAGE_SIZE 4096u

/* Segment selectors (see gdt.c). */
#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define USER_CS   0x1B
#define USER_DS   0x23

/* ---- port I/O and CPU helpers ---- */
static inline void outb(u16 port, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(port)); }
static inline void outw(u16 port, u16 v) { __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(port)); }
static inline u8 inb(u16 port) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline void io_wait(void) { outb(0x80, 0); }

/* Single CPU: "locking" means turning interrupts off. */
static inline u32 irq_save(void)
{
    u32 flags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
static inline void irq_restore(u32 flags)
{
    if (flags & 0x200)
        __asm__ volatile("sti" ::: "memory");
}

/* Saved CPU state, laid out exactly as boot.S pushes it. */
struct regs {
    u32 gs, fs, es, ds;
    u32 edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;  /* pusha */
    u32 int_no, err_code;
    u32 eip, cs, eflags, useresp, ss;                  /* pushed by the CPU */
};

/* ---- string.c ---- */
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
void strlcpy(char *d, const char *s, size_t n);
int atoi(const char *s);

/* ---- console.c ---- */
enum {
    BLACK, BLUE, GREEN, CYAN, RED, MAGENTA, BROWN, LIGHT_GREY,
    DARK_GREY, LIGHT_BLUE, LIGHT_GREEN, LIGHT_CYAN, LIGHT_RED, LIGHT_MAGENTA, YELLOW, WHITE,
};
void console_init(void);
void console_clear(void);
void console_set_color(u8 fg);
void console_write(const char *s, size_t n);
void kputc(char c);
void kputs(const char *s);
int kprintf(const char *fmt, ...);
void kpanic(const char *fmt, ...) __attribute__((noreturn));

/* ---- gdt.c ---- */
void gdt_init(void);
void tss_set_kernel_stack(u32 esp0);

/* ---- idt.c ---- */
typedef void (*irq_handler_t)(struct regs *r);
void idt_init(void);
void irq_install(int irq, irq_handler_t handler);
extern volatile int need_resched;
extern int scheduler_running;

/* ---- timer.c ---- */
#define TIMER_HZ 100
extern volatile u32 ticks;
void timer_init(u32 hz);

/* ---- input.c ---- */
void input_init(void);
int kgetc(void);
void input_flush(void);
extern volatile int ctrlc_pending;

/* ---- mm.c ---- */
struct multiboot_info {
    u32 flags, mem_lower, mem_upper, boot_device, cmdline;
    u32 mods_count, mods_addr, syms[4];
    u32 mmap_length, mmap_addr;
} __attribute__((packed));

struct mm_stats {
    u32 total_pages, free_pages;
    u32 heap_used, heap_free, heap_blocks;
    u32 kernel_size;
};
void mm_init(struct multiboot_info *mbi);
void *pmm_alloc_page(void);
void pmm_free_page(void *page);
void vmm_set_user(u32 addr, u32 len, bool user);
bool vmm_is_user_range(u32 addr, u32 len);
void *kmalloc(size_t size);
void kfree(void *p);
void mm_get_stats(struct mm_stats *s);

/* ---- task.c ---- */
enum task_state { T_UNUSED, T_READY, T_RUNNING, T_SLEEPING, T_BLOCKED, T_ZOMBIE, T_DEAD };

struct task {
    int pid;
    enum task_state state;
    char name[16];
    bool user;
    u32 esp;         /* saved stack pointer (points at a struct regs) */
    void *kstack;
    u32 kstack_top;
    void *ustack;
    u32 wake_tick;
    u32 cpu_ticks;
    int exit_code;
};

struct task_info {
    int pid;
    enum task_state state;
    char name[16];
    bool user;
    u32 cpu_ticks;
};

#define MAX_TASKS 32
extern struct task *current;
void tasks_init(void);
int task_spawn_kernel(const char *name, void (*entry)(void *), void *arg);
int task_spawn_user(const char *name, int (*main)(void));
struct regs *schedule(struct regs *r);
void task_exit_current(int code);
int task_kill(int pid, int code);
int task_status(int pid, int *exit_code);
int task_snapshot(struct task_info *out, int max);
const char *task_state_name(enum task_state s);
void ksleep(u32 ms);
void yield(void);

/* ---- syscall.c ---- */
void syscall_handler(struct regs *r);

/* ---- shell.c ---- */
void shell_main(void *arg);

#endif
