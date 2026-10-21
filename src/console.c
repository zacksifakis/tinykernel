/* Console output: VGA text mode plus a mirror on the COM1 serial port. */
#include "kernel.h"

#define VGA      ((volatile u16 *)0xB8000)
#define VGA_COLS 80
#define VGA_ROWS 25
#define COM1     0x3F8

static int row, col;
static u8 color = LIGHT_GREY;

/* VGA palette index -> ANSI SGR code, so the serial console gets colours too. */
static const u8 ansi_codes[16] = { 30, 34, 32, 36, 31, 35, 33, 0, 90, 94, 92, 96, 91, 95, 93, 97 };

static void serial_init(void)
{
    outb(COM1 + 1, 0x00); /* interrupts off while configuring */
    outb(COM1 + 3, 0x80); /* DLAB on */
    outb(COM1 + 0, 0x01); /* divisor 1 = 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8N1 */
    outb(COM1 + 2, 0xC7); /* FIFO on, cleared */
    outb(COM1 + 4, 0x0B); /* DTR, RTS, OUT2 (OUT2 gates the IRQ line) */
    outb(COM1 + 1, 0x01); /* interrupt on received data */
}

static void serial_putc(char c)
{
    while (!(inb(COM1 + 5) & 0x20))
        ;
    outb(COM1, (u8)c);
}

static void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

static void vga_update_cursor(void)
{
    u16 pos = (u16)(row * VGA_COLS + col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
    outb(0x3D4, 0x0E);
    outb(0x3D5, pos >> 8);
}

static void vga_scroll(void)
{
    for (int i = 0; i < VGA_COLS * (VGA_ROWS - 1); i++)
        VGA[i] = VGA[i + VGA_COLS];
    for (int i = 0; i < VGA_COLS; i++)
        VGA[VGA_COLS * (VGA_ROWS - 1) + i] = ' ' | (color << 8);
    row = VGA_ROWS - 1;
}

static void vga_putc(char c)
{
    switch (c) {
    case '\n': col = 0; row++; break;
    case '\r': col = 0; break;
    case '\b': if (col > 0) col--; break;
    case '\t': col = (col + 8) & ~7; break;
    default:
        VGA[row * VGA_COLS + col] = (u8)c | (color << 8);
        col++;
    }
    if (col >= VGA_COLS) {
        col = 0;
        row++;
    }
    if (row >= VGA_ROWS)
        vga_scroll();
}

static void putc_raw(char c)
{
    vga_putc(c);
    if (c == '\n')
        serial_putc('\r');
    serial_putc(c);
}

void console_init(void)
{
    serial_init();
    console_clear();
}

void console_clear(void)
{
    u32 f = irq_save();
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        VGA[i] = ' ' | (color << 8);
    row = col = 0;
    vga_update_cursor();
    serial_puts("\x1b[2J\x1b[H");
    irq_restore(f);
}

void console_set_color(u8 fg)
{
    u32 f = irq_save();
    color = fg & 0x0F;
    char seq[8] = "\x1b[";
    int n = 2, code = ansi_codes[color];
    if (code >= 10)
        seq[n++] = '0' + code / 10;
    seq[n++] = '0' + code % 10;
    seq[n++] = 'm';
    seq[n] = 0;
    serial_puts(seq);
    irq_restore(f);
}

void console_write(const char *s, size_t n)
{
    u32 f = irq_save();
    while (n--)
        putc_raw(*s++);
    vga_update_cursor();
    irq_restore(f);
}

void kputc(char c)
{
    console_write(&c, 1);
}

void kputs(const char *s)
{
    console_write(s, strlen(s));
}

/* ---- printf ---- */

struct out {
    char *buf;
    size_t cap, len;
};

static void out_c(struct out *o, char c)
{
    if (o->len + 1 < o->cap)
        o->buf[o->len] = c;
    o->len++;
}

static int utoa(u32 v, u32 base, char *tmp, bool upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char rev[12];
    int n = 0;
    do {
        rev[n++] = digits[v % base];
        v /= base;
    } while (v);
    for (int i = 0; i < n; i++)
        tmp[i] = rev[n - 1 - i];
    return n;
}

/* Supports %d %i %u %x %X %p %c %s %% with '-' / '0' flags and a width. */
static int kvsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
    struct out o = { buf, cap, 0 };

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out_c(&o, *fmt);
            continue;
        }
        fmt++;
        bool left = false, zero = false;
        for (;; fmt++) {
            if (*fmt == '-')
                left = true;
            else if (*fmt == '0')
                zero = true;
            else
                break;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l')
            fmt++;
        if (!*fmt)
            break;

        char tmp[16];
        const char *s = tmp;
        int len = 0;
        bool neg = false;
        switch (*fmt) {
        case 'd':
        case 'i': {
            int v = va_arg(ap, int);
            neg = v < 0;
            len = utoa(neg ? -(u32)v : (u32)v, 10, tmp, false);
            break;
        }
        case 'u': len = utoa(va_arg(ap, u32), 10, tmp, false); break;
        case 'x': len = utoa(va_arg(ap, u32), 16, tmp, false); break;
        case 'X': len = utoa(va_arg(ap, u32), 16, tmp, true); break;
        case 'p':
            out_c(&o, '0');
            out_c(&o, 'x');
            zero = true;
            width = 8;
            len = utoa(va_arg(ap, u32), 16, tmp, false);
            break;
        case 'c': tmp[0] = (char)va_arg(ap, int); len = 1; break;
        case 's':
            s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            len = strlen(s);
            break;
        default: tmp[0] = '%'; tmp[1] = *fmt; len = 2; break;
        case '%': tmp[0] = '%'; len = 1; break;
        }

        int pad = width - len - neg;
        if (!left && !zero)
            for (; pad > 0; pad--)
                out_c(&o, ' ');
        if (neg)
            out_c(&o, '-');
        if (!left && zero)
            for (; pad > 0; pad--)
                out_c(&o, '0');
        for (int i = 0; i < len; i++)
            out_c(&o, s[i]);
        if (left)
            for (; pad > 0; pad--)
                out_c(&o, ' ');
    }
    if (cap)
        buf[o.len < cap ? o.len : cap - 1] = 0;
    return o.len;
}

int kprintf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    console_write(buf, n < (int)sizeof buf ? (size_t)n : sizeof buf - 1);
    return n;
}

void kpanic(const char *fmt, ...)
{
    __asm__ volatile("cli");
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    console_set_color(LIGHT_RED);
    kputs("\n*** KERNEL PANIC ***\n");
    kputs(buf);
    kputs("\nSystem halted.\n");
    for (;;)
        __asm__ volatile("cli; hlt");
}
