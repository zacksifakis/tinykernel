/* The contract between the kernel and user programs. */
#ifndef ABI_H
#define ABI_H

/* System call numbers: eax = number, ebx/ecx = arguments, result in eax. */
enum {
    SYS_EXIT   = 0, /* exit(code)            */
    SYS_WRITE  = 1, /* write(buf, len) -> n  */
    SYS_GETPID = 2, /* getpid() -> pid       */
    SYS_YIELD  = 3, /* yield()               */
    SYS_SLEEP  = 4, /* sleep(ms)             */
    SYS_UPTIME = 5, /* uptime() -> ms        */
};

struct user_program {
    const char *name;
    const char *desc;
    int (*main)(void);
};

/* Defined in user/user.c, which is linked into the user-accessible pages. */
extern const struct user_program user_programs[];
extern const int user_program_count;
void user_start(int (*main)(void));

#endif
