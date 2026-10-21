/*
 * Tasks and a preemptive round-robin scheduler.
 *
 * Every task owns a kernel stack. When a task is interrupted, its whole CPU
 * state is pushed onto that stack as a struct regs; switching tasks is just
 * returning a different task's saved frame to isr_common.
 */
#include "kernel.h"

#define KSTACK_SIZE 8192

static struct task tasks[MAX_TASKS];
struct task *current;
static int next_pid = 1;

extern char boot_stack_top[];

static const char *state_names[] = {
    [T_UNUSED] = "unused", [T_READY] = "ready", [T_RUNNING] = "running",
    [T_SLEEPING] = "sleeping", [T_BLOCKED] = "blocked", [T_ZOMBIE] = "zombie", [T_DEAD] = "dead",
};

const char *task_state_name(enum task_state s)
{
    return state_names[s];
}

void tasks_init(void)
{
    /* Task 0 is whatever is running now: kmain becomes the idle task. */
    struct task *idle = &tasks[0];
    idle->pid = 0;
    strlcpy(idle->name, "idle", sizeof idle->name);
    idle->state = T_RUNNING;
    idle->kstack_top = (u32)boot_stack_top;
    current = idle;
}

static struct task *alloc_task(const char *name, bool user)
{
    for (int i = 1; i < MAX_TASKS; i++) {
        struct task *t = &tasks[i];
        if (t->state == T_UNUSED || t->state == T_DEAD) {
            memset(t, 0, sizeof *t);
            t->pid = next_pid++;
            strlcpy(t->name, name, sizeof t->name);
            t->user = user;
            t->kstack = kmalloc(KSTACK_SIZE);
            if (!t->kstack)
                return NULL;
            t->kstack_top = (u32)t->kstack + KSTACK_SIZE;
            return t;
        }
    }
    return NULL;
}

static void kthread_return(void)
{
    __asm__ volatile("int $0x80" :: "a"(SYS_EXIT), "b"(0));
    for (;;)
        ;
}

int task_spawn_kernel(const char *name, void (*entry)(void *), void *arg)
{
    u32 f = irq_save();
    struct task *t = alloc_task(name, false);
    if (!t) {
        irq_restore(f);
        return -1;
    }

    /* Stack the entry function sees: [return address][arg] */
    u32 *sp = (u32 *)t->kstack_top;
    *--sp = (u32)arg;
    *--sp = (u32)kthread_return;

    /* Same-ring iret pops no esp/ss, so the frame stops short of those fields. */
    struct regs *fr = (struct regs *)((u8 *)sp - (sizeof(struct regs) - 8));
    memset(fr, 0, sizeof(struct regs) - 8);
    fr->gs = fr->fs = fr->es = fr->ds = KERNEL_DS;
    fr->cs = KERNEL_CS;
    fr->eflags = 0x202; /* IF on */
    fr->eip = (u32)entry;

    t->esp = (u32)fr;
    t->state = T_READY;
    irq_restore(f);
    return t->pid;
}

int task_spawn_user(const char *name, int (*main)(void))
{
    u32 f = irq_save();
    struct task *t = alloc_task(name, true);
    void *ustack = t ? pmm_alloc_page() : NULL;
    if (!ustack) {
        if (t) {
            kfree(t->kstack);
            t->state = T_UNUSED;
        }
        irq_restore(f);
        return -1;
    }
    memset(ustack, 0, PAGE_SIZE);
    vmm_set_user((u32)ustack, PAGE_SIZE, true);
    t->ustack = ustack;

    /* User stack as user_start(main) expects it: [fake return][main] */
    u32 *usp = (u32 *)((u8 *)ustack + PAGE_SIZE);
    *--usp = (u32)main;
    *--usp = 0;

    struct regs *fr = (struct regs *)(t->kstack_top - sizeof(struct regs));
    memset(fr, 0, sizeof *fr);
    fr->gs = fr->fs = fr->es = fr->ds = USER_DS;
    fr->cs = USER_CS;
    fr->ss = USER_DS;
    fr->useresp = (u32)usp;
    fr->eflags = 0x202;
    fr->eip = (u32)user_start;

    t->esp = (u32)fr;
    t->state = T_READY;
    irq_restore(f);
    return t->pid;
}

static void reap(struct task *t)
{
    kfree(t->kstack);
    t->kstack = NULL;
    if (t->ustack) {
        vmm_set_user((u32)t->ustack, PAGE_SIZE, false);
        pmm_free_page(t->ustack);
        t->ustack = NULL;
    }
    t->state = T_DEAD; /* slot keeps pid + exit code until reused */
}

/* Called with interrupts off, from interrupt_dispatch. */
struct regs *schedule(struct regs *r)
{
    current->esp = (u32)r;
    if (current->state == T_RUNNING)
        current->state = T_READY;

    for (int i = 0; i < MAX_TASKS; i++) {
        struct task *t = &tasks[i];
        if (t->state == T_SLEEPING && (i32)(ticks - t->wake_tick) >= 0)
            t->state = T_READY;
        else if (t->state == T_ZOMBIE && t != current)
            reap(t); /* can't free the stack we're standing on */
    }

    /* Round robin, starting after the current task. Idle only if nobody else can run. */
    int cur = current - tasks;
    struct task *next = &tasks[0];
    for (int k = 1; k <= MAX_TASKS; k++) {
        struct task *t = &tasks[(cur + k) % MAX_TASKS];
        if (t != &tasks[0] && t->state == T_READY) {
            next = t;
            break;
        }
    }

    next->state = T_RUNNING;
    current = next;
    tss_set_kernel_stack(next->kstack_top);
    return (struct regs *)next->esp;
}

void task_exit_current(int code)
{
    current->exit_code = code;
    current->state = T_ZOMBIE;
    need_resched = 1;
}

static bool alive(struct task *t)
{
    return t->state == T_READY || t->state == T_RUNNING ||
           t->state == T_SLEEPING || t->state == T_BLOCKED;
}

int task_kill(int pid, int code)
{
    if (pid <= 1)
        return -1; /* idle and the shell are not killable */
    u32 f = irq_save();
    for (int i = 0; i < MAX_TASKS; i++) {
        struct task *t = &tasks[i];
        if (t->pid == pid && alive(t)) {
            if (t == current) {
                task_exit_current(code);
            } else {
                t->exit_code = code;
                t->state = T_ZOMBIE;
            }
            irq_restore(f);
            return 0;
        }
    }
    irq_restore(f);
    return -1;
}

/* 1 = still running, 0 = finished (exit code stored), -1 = no such task. */
int task_status(int pid, int *exit_code)
{
    int result = -1;
    u32 f = irq_save();
    for (int i = 0; i < MAX_TASKS; i++) {
        struct task *t = &tasks[i];
        if (t->state == T_UNUSED || t->pid != pid)
            continue;
        if (alive(t)) {
            result = 1;
        } else {
            *exit_code = t->exit_code;
            result = 0;
        }
        break;
    }
    irq_restore(f);
    return result;
}

int task_snapshot(struct task_info *out, int max)
{
    int n = 0;
    u32 f = irq_save();
    for (int i = 0; i < MAX_TASKS && n < max; i++) {
        struct task *t = &tasks[i];
        if (t->state == T_UNUSED || t->state == T_DEAD)
            continue;
        out[n].pid = t->pid;
        out[n].state = t->state;
        out[n].user = t->user;
        out[n].cpu_ticks = t->cpu_ticks;
        strlcpy(out[n].name, t->name, sizeof out[n].name);
        n++;
    }
    irq_restore(f);
    return n;
}

void yield(void)
{
    __asm__ volatile("int $0x80" :: "a"(SYS_YIELD) : "memory");
}

void ksleep(u32 ms)
{
    u32 f = irq_save();
    u32 t = (ms * TIMER_HZ + 999) / 1000;
    current->wake_tick = ticks + (t ? t : 1);
    current->state = T_SLEEPING;
    irq_restore(f);
    yield();
}
