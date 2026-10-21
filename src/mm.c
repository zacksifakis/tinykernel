/*
 * Memory management:
 *   - a bitmap physical page allocator built from the bootloader's memory map
 *   - paging: identity-mapped 4 KB pages, kernel-only unless explicitly
 *     handed to user mode, with page 0 left unmapped to catch NULL derefs
 *   - a first-fit kernel heap (kmalloc/kfree) with block coalescing
 */
#include "kernel.h"

#define MAX_MEM    (128u * 1024 * 1024)
#define MAX_PAGES  (MAX_MEM / PAGE_SIZE)
#define HEAP_SIZE  (4u * 1024 * 1024)
#define HEAP_MAGIC 0xC0FFEE42u

#define PTE_PRESENT 0x1
#define PTE_WRITE   0x2
#define PTE_USER    0x4

extern char __kernel_start[], __kernel_end[], __user_start[], __user_end[];

/* ---- physical pages ---- */

static u32 bitmap[MAX_PAGES / 32]; /* 1 = used */
static u32 total_pages, free_pages, search_hint;

static bool page_used(u32 p) { return bitmap[p >> 5] & (1u << (p & 31)); }
static void page_set(u32 p) { bitmap[p >> 5] |= 1u << (p & 31); }
static void page_clear(u32 p) { bitmap[p >> 5] &= ~(1u << (p & 31)); }

static void pmm_add_region(u64 base, u64 len)
{
    u64 start = (base + PAGE_SIZE - 1) & ~(u64)(PAGE_SIZE - 1);
    u64 end = (base + len) & ~(u64)(PAGE_SIZE - 1);
    if (end > MAX_MEM)
        end = MAX_MEM;
    for (u64 a = start; a < end; a += PAGE_SIZE) {
        u32 p = (u32)(a >> 12);
        if (page_used(p)) {
            page_clear(p);
            total_pages++;
            free_pages++;
        }
    }
}

static void pmm_reserve(u32 base, u32 len)
{
    for (u32 a = base & ~(PAGE_SIZE - 1); a < base + len && a < MAX_MEM; a += PAGE_SIZE) {
        u32 p = a >> 12;
        if (!page_used(p)) {
            page_set(p);
            free_pages--;
        }
    }
}

void *pmm_alloc_page(void)
{
    u32 f = irq_save();
    for (u32 n = 0; n < MAX_PAGES; n++) {
        u32 p = (search_hint + n) % MAX_PAGES;
        if (!page_used(p)) {
            page_set(p);
            free_pages--;
            search_hint = p + 1;
            irq_restore(f);
            return (void *)(p * PAGE_SIZE);
        }
    }
    irq_restore(f);
    return NULL;
}

void pmm_free_page(void *page)
{
    u32 f = irq_save();
    u32 p = (u32)page >> 12;
    if (page_used(p)) {
        page_clear(p);
        free_pages++;
    }
    irq_restore(f);
}

/* ---- paging ---- */

static u32 page_dir[1024] __attribute__((aligned(4096)));
static u32 page_tables[MAX_MEM / (PAGE_SIZE * 1024)][1024] __attribute__((aligned(4096)));

static u32 *pte_for(u32 addr)
{
    u32 p = addr >> 12;
    return &page_tables[p >> 10][p & 1023];
}

void vmm_set_user(u32 addr, u32 len, bool user)
{
    for (u32 a = addr & ~(PAGE_SIZE - 1); a < addr + len; a += PAGE_SIZE) {
        u32 *pte = pte_for(a);
        *pte = user ? (*pte | PTE_USER) : (*pte & ~PTE_USER);
        __asm__ volatile("invlpg (%0)" :: "r"(a) : "memory");
    }
}

/* Is [addr, addr+len) entirely mapped and user-accessible? */
bool vmm_is_user_range(u32 addr, u32 len)
{
    if (len == 0)
        return true;
    if (addr + len < addr || addr + len > MAX_MEM)
        return false;
    for (u32 a = addr & ~(PAGE_SIZE - 1); a < addr + len; a += PAGE_SIZE) {
        u32 pte = *pte_for(a);
        if ((pte & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER))
            return false;
    }
    return true;
}

static void paging_init(void)
{
    u32 ntables = MAX_MEM / (PAGE_SIZE * 1024);
    for (u32 t = 0; t < ntables; t++) {
        for (u32 i = 0; i < 1024; i++)
            page_tables[t][i] = ((t * 1024 + i) * PAGE_SIZE) | PTE_PRESENT | PTE_WRITE;
        /* The directory allows user access; each page table entry decides. */
        page_dir[t] = (u32)page_tables[t] | PTE_PRESENT | PTE_WRITE | PTE_USER;
    }
    page_tables[0][0] = 0; /* NULL guard page */

    for (u32 a = (u32)__user_start; a < (u32)__user_end; a += PAGE_SIZE)
        *pte_for(a) |= PTE_USER;

    __asm__ volatile("mov %0, %%cr3" :: "r"(page_dir));
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; /* PG */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
}

/* ---- kernel heap ---- */

struct block {
    u32 size; /* payload bytes */
    u32 free;
    struct block *next;
    u32 magic;
};

static struct block *heap_head;

static void heap_init(u32 base, u32 size)
{
    heap_head = (struct block *)base;
    heap_head->size = size - sizeof(struct block);
    heap_head->free = 1;
    heap_head->next = NULL;
    heap_head->magic = HEAP_MAGIC;
}

void *kmalloc(size_t size)
{
    size = (size + 15) & ~15u;
    u32 f = irq_save();
    for (struct block *b = heap_head; b; b = b->next) {
        if (!b->free || b->size < size)
            continue;
        if (b->size >= size + sizeof(struct block) + 16) {
            struct block *rest = (struct block *)((u8 *)(b + 1) + size);
            rest->size = b->size - size - sizeof(struct block);
            rest->free = 1;
            rest->next = b->next;
            rest->magic = HEAP_MAGIC;
            b->size = size;
            b->next = rest;
        }
        b->free = 0;
        irq_restore(f);
        return b + 1;
    }
    irq_restore(f);
    return NULL;
}

void kfree(void *p)
{
    if (!p)
        return;
    struct block *b = (struct block *)p - 1;
    if (b->magic != HEAP_MAGIC || b->free)
        kpanic("kfree: bad pointer %p", p);
    u32 f = irq_save();
    b->free = 1;
    /* Blocks are kept in address order, so neighbours in the list are neighbours in memory. */
    for (struct block *c = heap_head; c && c->next;) {
        if (c->free && c->next->free) {
            c->size += sizeof(struct block) + c->next->size;
            c->next = c->next->next;
        } else {
            c = c->next;
        }
    }
    irq_restore(f);
}

/* ---- setup ---- */

struct mb_mmap_entry {
    u32 size;
    u64 addr, len;
    u32 type;
} __attribute__((packed));

void mm_init(struct multiboot_info *mbi)
{
    memset(bitmap, 0xFF, sizeof bitmap);

    if (mbi->flags & (1 << 6)) {
        u32 p = mbi->mmap_addr, end = mbi->mmap_addr + mbi->mmap_length;
        while (p < end) {
            struct mb_mmap_entry *e = (struct mb_mmap_entry *)p;
            if (e->type == 1)
                pmm_add_region(e->addr, e->len);
            p += e->size + sizeof e->size;
        }
    } else if (mbi->flags & 1) {
        pmm_add_region(0x100000, (u64)mbi->mem_upper * 1024);
    } else {
        kpanic("bootloader gave no memory information");
    }

    u32 kstart = (u32)__kernel_start;
    u32 kend = ((u32)__kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    pmm_reserve(0, 0x100000);           /* BIOS, VGA, real-mode leftovers */
    pmm_reserve(kstart, kend - kstart); /* the kernel image itself */
    pmm_reserve(kend, HEAP_SIZE);
    if (free_pages < 256)
        kpanic("not enough memory (%u KB free)", free_pages * 4);

    heap_init(kend, HEAP_SIZE);
    paging_init();
}

void mm_get_stats(struct mm_stats *s)
{
    u32 f = irq_save();
    s->total_pages = total_pages;
    s->free_pages = free_pages;
    s->heap_used = s->heap_free = s->heap_blocks = 0;
    for (struct block *b = heap_head; b; b = b->next) {
        s->heap_blocks++;
        if (b->free)
            s->heap_free += b->size;
        else
            s->heap_used += b->size;
    }
    s->kernel_size = (u32)__kernel_end - (u32)__kernel_start;
    irq_restore(f);
}
