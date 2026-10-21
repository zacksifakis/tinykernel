ENTRY(_start)

SECTIONS
{
    . = 1M;
    __kernel_start = .;

    .text ALIGN(4K) : {
        *(.multiboot)
        *(EXCLUDE_FILE(*user.o) .text .text.*)
    }
    .rodata ALIGN(4K) : { *(EXCLUDE_FILE(*user.o) .rodata .rodata.*) }
    .data ALIGN(4K) : { *(EXCLUDE_FILE(*user.o) .data .data.*) }

    /* Everything from user.o, on pages of its own that ring 3 is allowed to use. */
    .user ALIGN(4K) : {
        __user_start = .;
        *user.o(.text .text.* .rodata .rodata.* .data .data.* .bss .bss.*)
        . = ALIGN(4K);
        __user_end = .;
    }

    .bss ALIGN(4K) : { *(COMMON) *(.bss .bss.*) }

    . = ALIGN(4K);
    __kernel_end = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
