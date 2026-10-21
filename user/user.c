/*
 * User programs. This file is linked into its own page-aligned section that is
 * the only kernel-image memory ring 3 may touch, so nothing here may call into
 * the kernel except through int 0x80. (The Makefile checks for that.)
 */
#include "abi.h"

static inline int syscall(int n, int a, int b)
{
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a), "c"(b) : "memory");
    return ret;
}

static int ustrlen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void print(const char *s) { syscall(SYS_WRITE, (int)s, ustrlen(s)); }
static int getpid(void) { return syscall(SYS_GETPID, 0, 0); }
static void sleep_ms(int ms) { syscall(SYS_SLEEP, ms, 0); }
static unsigned uptime_ms(void) { return (unsigned)syscall(SYS_UPTIME, 0, 0); }

static void print_int(int v)
{
    char buf[12];
    int i = sizeof buf - 1;
    unsigned u = v < 0 ? -(unsigned)v : (unsigned)v;
    buf[i] = 0;
    do {
        buf[--i] = '0' + u % 10;
        u /= 10;
    } while (u);
    if (v < 0)
        buf[--i] = '-';
    print(&buf[i]);
}

/* Entry point for every user task: run main, then exit with its result. */
void user_start(int (*main)(void))
{
    syscall(SYS_EXIT, main(), 0);
    for (;;)
        ;
}

static int hello_main(void)
{
    print("Hello from user space! I'm pid ");
    print_int(getpid());
    print(", running in ring 3.\n");
    return 0;
}

static int counter_main(void)
{
    int pid = getpid();
    for (int i = 1; i <= 5; i++) {
        print("[counter ");
        print_int(pid);
        print("] tick ");
        print_int(i);
        print("/5\n");
        sleep_ms(500);
    }
    return 0;
}

static int primes_main(void)
{
    unsigned start = uptime_ms();
    int count = 0;
    print("primes below 100: ");
    for (int n = 2; n < 100; n++) {
        int prime = 1;
        for (int d = 2; d * d <= n; d++)
            if (n % d == 0)
                prime = 0;
        if (prime) {
            print_int(n);
            print(" ");
            count++;
        }
    }
    print("\nfound ");
    print_int(count);
    print(" in ");
    print_int((int)(uptime_ms() - start));
    print(" ms\n");
    return 0;
}

static int spin_main(void)
{
    /* Never yields or sleeps. The timer interrupt preempts it anyway. */
    int pid = getpid();
    for (int round = 1;; round++) {
        unsigned until = uptime_ms() + 2000;
        while (uptime_ms() < until)
            for (unsigned i = 0; i < 100000; i++)
                __asm__ volatile("");
        print("[spin ");
        print_int(pid);
        print("] busy loop round ");
        print_int(round);
        print("\n");
    }
    return 0;
}

static int segv_main(void)
{
    print("Trying to overwrite kernel code at 0x00100000...\n");
    *(volatile unsigned *)0x00100000 = 0xDEADBEEF;
    print("...this should never print\n");
    return 1;
}

static int null_main(void)
{
    print("Dereferencing a NULL pointer...\n");
    return *(volatile int *)0;
}

static int cli_main(void)
{
    print("Trying to disable interrupts (cli) from ring 3...\n");
    __asm__ volatile("cli");
    print("...this should never print\n");
    return 1;
}

static int badptr_main(void)
{
    print("Asking the kernel to write() from a kernel address...\n");
    int r = syscall(SYS_WRITE, 0x00100000, 64);
    print("write() returned ");
    print_int(r);
    print(r < 0 ? " -- the kernel refused, as it should.\n" : " -- uh oh, that leaked kernel memory.\n");
    return 0;
}

static int exit_main(void)
{
    print("Exiting with status 42.\n");
    return 42;
}

const struct user_program user_programs[] = {
    { "hello",   "say hello from ring 3",                            hello_main },
    { "counter", "count to 5, sleeping between ticks",               counter_main },
    { "primes",  "compute some primes",                              primes_main },
    { "spin",    "burn CPU forever (try it with &, then ps / kill)", spin_main },
    { "segv",    "write to kernel memory -> page fault",             segv_main },
    { "null",    "dereference NULL -> page fault",                   null_main },
    { "cli",     "privileged instruction -> protection fault",       cli_main },
    { "badptr",  "pass a kernel pointer to a syscall",               badptr_main },
    { "exit42",  "exit with a nonzero status",                       exit_main },
};
const int user_program_count = sizeof user_programs / sizeof user_programs[0];
