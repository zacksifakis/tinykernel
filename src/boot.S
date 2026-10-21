# Entry point, GDT reload, and interrupt stubs.

.set MB_MAGIC,    0x1BADB002
.set MB_FLAGS,    0x00000003          # page-align modules | give us a memory map
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM

.section .bss, "aw", @nobits
.align 16
.skip 16384
.global boot_stack_top
boot_stack_top:

.section .text

# The bootloader leaves us in 32-bit protected mode, paging off,
# eax = multiboot magic, ebx = multiboot info pointer.
.global _start
.type _start, @function
_start:
    movl $boot_stack_top, %esp
    pushl $0
    popfl
    pushl %ebx
    pushl %eax
    call kmain
1:  cli
    hlt
    jmp 1b

# void gdt_flush(struct gdt_ptr *)
.global gdt_flush
gdt_flush:
    movl 4(%esp), %eax
    lgdt (%eax)
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    ljmp $0x08, $2f
2:  movw $0x28, %ax                   # TSS selector
    ltr %ax
    ret

# ---- interrupt stubs ----
# Every stub pushes (error code, vector) so the frame always has the same shape.

.macro ISR_NOERR num
.global isr\num
isr\num:
    pushl $0
    pushl $\num
    jmp isr_common
.endm

.macro ISR_ERR num
.global isr\num
isr\num:
    pushl $\num
    jmp isr_common
.endm

.irp i, 0,1,2,3,4,5,6,7,9,15,16,18,19,20,22,23,24,25,26,27,28,31
ISR_NOERR \i
.endr
.irp i, 8,10,11,12,13,14,17,21,29,30
ISR_ERR \i
.endr
.irp i, 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,128
ISR_NOERR \i
.endr

# Save everything, call interrupt_dispatch(regs), then resume whichever
# stack it returns. Returning a different task's frame is a context switch.
isr_common:
    pushal
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    cld
    pushl %esp
    call interrupt_dispatch
    movl %eax, %esp
    popl %gs
    popl %fs
    popl %es
    popl %ds
    popal
    addl $8, %esp                     # drop vector + error code
    iret

.section .rodata
.global isr_stub_table
isr_stub_table:
.irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
    .long isr\i
.endr
