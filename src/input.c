/* Keyboard (PS/2) and serial input, merged into one ring buffer. */
#include "kernel.h"

#define COM1     0x3F8
#define INBUF_SZ 256

static char inbuf[INBUF_SZ];
static volatile u32 head, tail;
static struct task *waiter;
volatile int ctrlc_pending;

static const char kbd_lower[58] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ',
};
static const char kbd_upper[58] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ',
};

/* Runs in interrupt context. */
static void input_push(char c)
{
    if (c == 3)
        ctrlc_pending = 1;
    u32 next = (head + 1) % INBUF_SZ;
    if (next == tail)
        return; /* full: drop */
    inbuf[head] = c;
    head = next;
    if (waiter && waiter->state == T_BLOCKED) {
        waiter->state = T_READY;
        need_resched = 1;
    }
    waiter = NULL;
}

static void keyboard_irq(struct regs *r)
{
    static bool shift, ctrl, caps, extended;
    (void)r;
    u8 sc = inb(0x60);

    if (sc == 0xE0) {
        extended = true;
        return;
    }
    if (extended) { /* arrows etc.: ignored */
        extended = false;
        return;
    }
    bool release = sc & 0x80;
    u8 key = sc & 0x7F;
    if (key == 0x2A || key == 0x36) {
        shift = !release;
        return;
    }
    if (key == 0x1D) {
        ctrl = !release;
        return;
    }
    if (key == 0x3A && !release)
        caps = !caps;
    if (release || key >= sizeof kbd_lower)
        return;

    char c = shift ? kbd_upper[key] : kbd_lower[key];
    if (caps && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        c ^= 0x20;
    if (ctrl && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        c &= 0x1F;
    if (c)
        input_push(c);
}

static void serial_irq(struct regs *r)
{
    (void)r;
    while (inb(COM1 + 5) & 1) {
        char c = (char)inb(COM1);
        if (c == '\r')
            c = '\n';
        else if (c == 0x7F)
            c = '\b';
        input_push(c);
    }
}

/* Blocks the calling kernel thread until a character arrives. */
int kgetc(void)
{
    for (;;) {
        u32 f = irq_save();
        if (head != tail) {
            char c = inbuf[tail];
            tail = (tail + 1) % INBUF_SZ;
            irq_restore(f);
            return c;
        }
        waiter = current;
        current->state = T_BLOCKED;
        irq_restore(f);
        yield();
    }
}

void input_flush(void)
{
    u32 f = irq_save();
    tail = head;
    irq_restore(f);
}

void input_init(void)
{
    inb(0x60);
    while (inb(COM1 + 5) & 1)
        inb(COM1);
    irq_install(1, keyboard_irq);
    irq_install(4, serial_irq);
}
