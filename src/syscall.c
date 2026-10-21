/* int 0x80 system calls. Interrupts stay off while a call is handled. */
#include "kernel.h"

#define EFAULT 14
#define ENOSYS 38

void syscall_handler(struct regs *r)
{
    switch (r->eax) {
    case SYS_EXIT:
        task_exit_current((int)r->ebx);
        break;

    case SYS_WRITE: {
        const char *buf = (const char *)r->ebx;
        u32 len = r->ecx;
        /* Never trust a pointer from user space. */
        if (current->user && !vmm_is_user_range((u32)buf, len)) {
            r->eax = (u32)-EFAULT;
            break;
        }
        console_write(buf, len);
        r->eax = len;
        break;
    }

    case SYS_GETPID:
        r->eax = current->pid;
        break;

    case SYS_YIELD:
        need_resched = 1;
        r->eax = 0;
        break;

    case SYS_SLEEP: {
        u32 t = (r->ebx * TIMER_HZ + 999) / 1000;
        current->wake_tick = ticks + (t ? t : 1);
        current->state = T_SLEEPING;
        need_resched = 1;
        r->eax = 0;
        break;
    }

    case SYS_UPTIME:
        r->eax = ticks * (1000 / TIMER_HZ);
        break;

    default:
        r->eax = (u32)-ENOSYS;
    }
}
