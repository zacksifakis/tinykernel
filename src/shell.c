/* A small interactive shell, running as a kernel thread. */
#include "kernel.h"

#define LINE_MAX 128
#define MAX_ARGS 8

struct command {
    const char *name;
    const char *usage;
    const char *help;
    void (*fn)(int argc, char **argv);
};

static const struct command commands[];

static int readline(char *buf, int max)
{
    int n = 0;
    for (;;) {
        int c = kgetc();
        if (c == '\n') {
            kputc('\n');
            buf[n] = 0;
            return n;
        }
        if (c == 3) { /* Ctrl-C */
            ctrlc_pending = 0;
            kputs("^C\n");
            buf[0] = 0;
            return 0;
        }
        if (c == '\b') {
            if (n > 0) {
                n--;
                kputs("\b \b");
            }
            continue;
        }
        if (c < 32 || c > 126)
            continue;
        if (n < max - 1) {
            buf[n++] = (char)c;
            kputc((char)c);
        }
    }
}

static int parse(char *line, char **argv)
{
    int argc = 0;
    while (*line && argc < MAX_ARGS) {
        while (*line == ' ')
            *line++ = 0;
        if (!*line)
            break;
        argv[argc++] = line;
        while (*line && *line != ' ')
            line++;
    }
    return argc;
}

static void cmd_help(int argc, char **argv)
{
    (void)argc, (void)argv;
    for (const struct command *c = commands; c->name; c++) {
        console_set_color(LIGHT_CYAN);
        kprintf("  %-22s", c->usage);
        console_set_color(LIGHT_GREY);
        kprintf("%s\n", c->help);
    }
}

static void cmd_clear(int argc, char **argv)
{
    (void)argc, (void)argv;
    console_clear();
}

static void cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        kprintf(i > 1 ? " %s" : "%s", argv[i]);
    kputc('\n');
}

static void cmd_uptime(int argc, char **argv)
{
    (void)argc, (void)argv;
    u32 ms = ticks * (1000 / TIMER_HZ);
    kprintf("up %u.%02u seconds (%u timer ticks)\n", ms / 1000, (ms % 1000) / 10, ticks);
}

static void cmd_mem(int argc, char **argv)
{
    (void)argc, (void)argv;
    struct mm_stats s;
    mm_get_stats(&s);
    kprintf("physical: %u KB usable, %u KB free (%u of %u pages in use)\n",
            s.total_pages * 4, s.free_pages * 4, s.total_pages - s.free_pages, s.total_pages);
    kprintf("kernel:   %u KB image\n", s.kernel_size / 1024);
    kprintf("heap:     %u KB used, %u KB free, %u blocks\n",
            s.heap_used / 1024, s.heap_free / 1024, s.heap_blocks);
}

static void cmd_ps(int argc, char **argv)
{
    (void)argc, (void)argv;
    struct task_info info[MAX_TASKS];
    int n = task_snapshot(info, MAX_TASKS);
    console_set_color(WHITE);
    kprintf("  PID  STATE     MODE    CPU(ms)  NAME\n");
    console_set_color(LIGHT_GREY);
    for (int i = 0; i < n; i++)
        kprintf("  %3d  %-8s  %-6s  %7u  %s\n", info[i].pid, task_state_name(info[i].state),
                info[i].user ? "user" : "kernel", info[i].cpu_ticks * (1000 / TIMER_HZ), info[i].name);
}

static void list_programs(void)
{
    kputs("programs (run in ring 3):\n");
    for (int i = 0; i < user_program_count; i++) {
        console_set_color(LIGHT_CYAN);
        kprintf("  %-10s", user_programs[i].name);
        console_set_color(LIGHT_GREY);
        kprintf("%s\n", user_programs[i].desc);
    }
}

static void cmd_run(int argc, char **argv)
{
    if (argc < 2) {
        list_programs();
        return;
    }
    const struct user_program *prog = NULL;
    for (int i = 0; i < user_program_count; i++)
        if (strcmp(user_programs[i].name, argv[1]) == 0)
            prog = &user_programs[i];
    if (!prog) {
        kprintf("run: no program named '%s'\n", argv[1]);
        list_programs();
        return;
    }

    int pid = task_spawn_user(prog->name, prog->main);
    if (pid < 0) {
        kputs("run: out of task slots or memory\n");
        return;
    }
    if (argc > 2 && strcmp(argv[2], "&") == 0) {
        kprintf("[%d] %s running in background\n", pid, prog->name);
        return;
    }

    /* Foreground: wait for it, letting Ctrl-C kill it. */
    ctrlc_pending = 0;
    int code;
    while (task_status(pid, &code) == 1) {
        if (ctrlc_pending) {
            ctrlc_pending = 0;
            input_flush();
            task_kill(pid, 130);
            kputs("^C\n");
        }
        ksleep(10);
    }
    if (code != 0) {
        console_set_color(DARK_GREY);
        kprintf("[pid %d exited with status %d]\n", pid, code);
        console_set_color(LIGHT_GREY);
    }
}

static void cmd_kill(int argc, char **argv)
{
    if (argc < 2) {
        kputs("usage: kill <pid>\n");
        return;
    }
    int pid = atoi(argv[1]);
    if (task_kill(pid, 137) < 0)
        kprintf("kill: can't kill pid %d\n", pid);
    else
        kprintf("killed %d\n", pid);
}

static void cmd_sleep(int argc, char **argv)
{
    if (argc < 2) {
        kputs("usage: sleep <ms>\n");
        return;
    }
    ksleep((u32)atoi(argv[1]));
}

static void cmd_reboot(int argc, char **argv)
{
    (void)argc, (void)argv;
    kputs("Rebooting...\n");
    while (inb(0x64) & 0x02)
        ;
    outb(0x64, 0xFE); /* pulse the CPU reset line via the keyboard controller */
    for (;;)
        __asm__ volatile("hlt");
}

static void cmd_shutdown(int argc, char **argv)
{
    (void)argc, (void)argv;
    kputs("Shutting down.\n");
    outw(0x604, 0x2000);  /* QEMU */
    outw(0xB004, 0x2000); /* Bochs, older QEMU */
    outw(0x4004, 0x3400); /* VirtualBox */
    __asm__ volatile("cli");
    kputs("It is now safe to turn off your computer.\n");
    for (;;)
        __asm__ volatile("hlt");
}

static const struct command commands[] = {
    { "help",     "help",           "show this list",                          cmd_help },
    { "run",      "run <prog> [&]", "start a user program (no args: list them)", cmd_run },
    { "ps",       "ps",             "list tasks",                              cmd_ps },
    { "kill",     "kill <pid>",     "terminate a task",                        cmd_kill },
    { "mem",      "mem",            "memory usage",                            cmd_mem },
    { "uptime",   "uptime",         "time since boot",                         cmd_uptime },
    { "echo",     "echo <text>",    "print text",                              cmd_echo },
    { "sleep",    "sleep <ms>",     "block the shell for a while",             cmd_sleep },
    { "clear",    "clear",          "clear the screen",                        cmd_clear },
    { "reboot",   "reboot",         "restart the machine",                     cmd_reboot },
    { "shutdown", "shutdown",       "power off",                               cmd_shutdown },
    { NULL, NULL, NULL, NULL },
};

void shell_main(void *arg)
{
    (void)arg;
    char line[LINE_MAX];
    char *argv[MAX_ARGS];

    kputs("\nType 'help' for commands, 'run' to see user programs.\n\n");
    for (;;) {
        console_set_color(LIGHT_GREEN);
        kputs("tiny");
        console_set_color(WHITE);
        kputs("> ");
        console_set_color(LIGHT_GREY);

        readline(line, sizeof line);
        int argc = parse(line, argv);
        if (argc == 0)
            continue;

        const struct command *c;
        for (c = commands; c->name; c++)
            if (strcmp(c->name, argv[0]) == 0)
                break;
        if (c->name)
            c->fn(argc, argv);
        else
            kprintf("%s: command not found\n", argv[0]);
    }
}
