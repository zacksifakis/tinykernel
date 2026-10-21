# tinykernel

A small 32-bit x86 operating system kernel written from scratch in C and assembly.
QEMU boots it directly through the Multiboot protocol.

## Build and run

Needs `clang` (Apple's clang works), `ld.lld`, and QEMU: `brew install lld qemu`.

```
make            # builds build/kernel.elf
make run        # QEMU window; serial console mirrored to this terminal
make run-term   # everything in the terminal (quit: Ctrl-A, then X)
make test       # boot headless, drive the shell over serial, check the output
```

Try `help`, `run`, `run hello`, `run spin &`, `ps`, `kill <pid>`, `run segv`, `mem`.

## What's inside

| File | What it does |
|------|--------------|
| `src/boot.S` | Multiboot header, entry point, GDT reload, interrupt stubs |
| `src/gdt.c` | Flat kernel and user segments, plus a TSS for ring 3 → ring 0 stack switches |
| `src/idt.c` | IDT, PIC remapping, the exception handler, and the central `interrupt_dispatch` |
| `src/mm.c` | Bitmap page allocator from the BIOS memory map, paging with user/kernel page protection, `kmalloc`/`kfree` heap |
| `src/task.c` | Tasks and a preemptive round-robin scheduler |
| `src/timer.c` | 100 Hz PIT tick that drives preemption |
| `src/input.c` | PS/2 keyboard and serial input sharing one buffer; blocking `kgetc` |
| `src/syscall.c` | `int 0x80` system calls: exit, write, getpid, yield, sleep, uptime |
| `src/console.c` | VGA text mode and serial output, `kprintf`, panic screen |
| `src/shell.c` | The interactive shell (a kernel thread) |
| `user/user.c` | Programs that run in ring 3 and reach the kernel only through syscalls |
| `tests/run.sh` | End-to-end test: shell commands, user programs, fault isolation, preemption, kill, Ctrl-C, heap leak check, shutdown |

### How the key pieces work

**Context switching.** Every interrupt saves the full CPU state (`struct regs`) on the
current task's kernel stack. Then it calls `interrupt_dispatch`, which returns the
frame to resume. To switch tasks, the scheduler returns a *different* task's frame.
A new task starts from a hand-built fake frame.

**Protection.** Pages are kernel-only by default. Only the `.user` section (everything
linked from `user.o`) and each process's stack page are marked user-accessible. Page 0
is unmapped so that NULL dereferences fault. When a ring-3 program faults, the kernel
kills that program and keeps running. A fault in ring 0 causes a kernel panic. The
`write` syscall checks that the buffer pointer lies in user memory before using it. The
Makefile refuses to link if user code references any kernel symbol.

**Memory map.** The kernel loads at 1 MB, the 4 MB heap comes right after it, and
128 MB is identity-mapped.

### Limits

This is a teaching kernel. It runs on a single CPU, has one shared address space
(isolation comes from page permissions, not separate page tables), has no filesystem,
and its programs are built into the kernel image. Natural next steps: per-process page
directories, an ELF loader with an initrd, a VFS, and `fork`/`exec`.
