#include <asm/mm.h>
#include <kernel/kernel.h>
#ifdef ENABLE_VGA
#include <drivers/vga.h>
#endif
#include <string.h>
#include <stdint.h>

static void* heap_start = NULL;
static void* heap_end = NULL;
static block_header_t* free_list = NULL;
static size_t total_heap_size = 0;
static size_t used_heap_size = 0;
static uint32_t kernel_page_directory[1024] __attribute__((aligned(4096)));
static uint8_t paging_enabled = 0;
static uint8_t user_slot_used[USER_SLOT_COUNT];
struct mm_map global_mmap;

#define CR0_PG 0x80000000u
#define CR4_PSE 0x00000010u
#define PDE_PRESENT 0x001u
#define PDE_RW 0x002u
#define PDE_USER 0x004u
#define PDE_PS 0x080u

static size_t align_size(size_t size) {
    return (size + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);
}

static inline size_t block_total_size(block_header_t* b) {
    return b->size + sizeof(block_header_t);
}

static void* get_block_payload(block_header_t* block) {
    return (void*)((uint8_t*)block + sizeof(block_header_t));
}

static block_header_t* get_block_from_payload(void* ptr) {
    if (!ptr) return NULL;
    return (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
}

static block_header_t* find_free_block(size_t needed_payload) {
    block_header_t* cur = free_list;
    while (cur) {
        if (cur->is_free && cur->size >= needed_payload)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

static void insert_into_list_sorted(block_header_t* block) {
    if (!free_list) {
        free_list = block;
        block->prev = NULL;
        block->next = NULL;
        return;
    }

    block_header_t* cur = free_list;
    block_header_t* prev = NULL;

    while (cur && cur < block) {
        prev = cur;
        cur = cur->next;
    }

    block->next = cur;
    block->prev = prev;

    if (prev)
        prev->next = block;
    else
        free_list = block;

    if (cur)
        cur->prev = block;
}

static void remove_from_list(block_header_t* block) {
    if (!block) return;
    
    if (block->prev)
        block->prev->next = block->next;
    else
        free_list = block->next;

    if (block->next)
        block->next->prev = block->prev;

    block->next = NULL;
    block->prev = NULL;
}

static block_header_t* split_block(block_header_t* block, size_t payload_size) {
    payload_size = align_size(payload_size);
    size_t remaining = block->size - payload_size;

    if (remaining >= sizeof(block_header_t) + MIN_BLOCK_SIZE) {
        block_header_t* new_block = (block_header_t*)
            ((uint8_t*)get_block_payload(block) + payload_size);

        new_block->magic = HEAP_MAGIC;
        new_block->size = remaining - sizeof(block_header_t);
        new_block->is_free = 1;
        new_block->next = NULL;
        new_block->prev = NULL;

        block->size = payload_size;

        insert_into_list_sorted(new_block);
        return new_block;
    }
    return NULL;
}

static void merge_free_blocks(block_header_t* block) {
    if (!block || !block->is_free) return;

    if (block->prev && block->prev->is_free && 
        (uint8_t*)block == (uint8_t*)block->prev + block_total_size(block->prev)) {
        block->prev->size += block_total_size(block);
        block->prev->next = block->next;
        if (block->next)
            block->next->prev = block->prev;
        block = block->prev;
    }

    if (block->next && block->next->is_free &&
        (uint8_t*)block->next == (uint8_t*)block + block_total_size(block)) {
        block->size += block_total_size(block->next);
        block->next = block->next->next;
        if (block->next)
            block->next->prev = block;
    }
}

void mm_init(void) {
    volatile struct mm_map_entry *entry = (volatile struct mm_map_entry *)MM_MAP_ADDRESS;
    global_mmap.count = 0;

    for (int i = 0; i < MM_MAX_ENTRIES; i++) {
        uint64_t base = ((uint64_t)entry[i].base_high << 32) | entry[i].base_low;
        uint64_t length = ((uint64_t)entry[i].length_high << 32) | entry[i].length_low;
        if (base == 0 && length == 0) break;
        if (global_mmap.count < MM_MAX_ENTRIES) {
            global_mmap.entries[global_mmap.count++] = entry[i];
        }
    }
}

void paging_init(void) {
    uint32_t cr0;
    uint32_t cr4;

    for (uint32_t i = 0; i < 1024; i++) {
        kernel_page_directory[i] = (i << 22) | PDE_PRESENT | PDE_RW | PDE_PS;
    }
    memset(user_slot_used, 0, sizeof(user_slot_used));

    __asm__ __volatile__("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_PSE;
    __asm__ __volatile__("mov %0, %%cr4" : : "r"(cr4) : "memory");

    __asm__ __volatile__("mov %0, %%cr3" : : "r"((uint32_t)kernel_page_directory) : "memory");

    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_PG;
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0) : "memory");

    paging_enabled = 1;
}

uint32_t mm_kernel_cr3(void) {
    return (uint32_t)kernel_page_directory;
}

void mm_switch_cr3(uint32_t cr3_phys) {
    if (!paging_enabled || !cr3_phys) return;
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(cr3_phys) : "memory");
}

int mm_user_slot_alloc(uint32_t *slot_idx_out, uint32_t *phys_base_out) {
    uint32_t idx;
    uint32_t phys;

    if (!slot_idx_out || !phys_base_out) return -1;

    for (idx = 0; idx < USER_SLOT_COUNT; idx++) {
        if (!user_slot_used[idx]) {
            user_slot_used[idx] = 1;
            phys = USER_SLOT_BASE_PHYS + idx * USER_SLOT_SIZE_PHYS;
            memset((void*)(uintptr_t)phys, 0, USER_SLOT_SIZE_PHYS);
            *slot_idx_out = idx;
            *phys_base_out = phys;
            return 0;
        }
    }
    return -1;
}

void mm_user_slot_free(uint32_t slot_idx) {
    if (slot_idx >= USER_SLOT_COUNT) return;
    user_slot_used[slot_idx] = 0;
}

uint32_t mm_user_cr3_create(uint32_t user_phys_base) {
    uint32_t *pd;
    uint32_t user_pde;

    if ((user_phys_base & (USER_SLOT_SIZE_PHYS - 1u)) != 0) return 0;
    pd = (uint32_t*)valloc_aligned(4096, 4096);
    if (!pd) return 0;

    memcpy(pd, kernel_page_directory, 4096);
    user_pde = (user_phys_base & 0xFFC00000u) | PDE_PRESENT | PDE_RW | PDE_USER | PDE_PS;
    pd[USER_VADDR_BASE >> 22] = user_pde;
    return (uint32_t)pd;
}

void mm_user_cr3_destroy(uint32_t cr3_phys) {
    if (!cr3_phys || cr3_phys == (uint32_t)kernel_page_directory) return;
    vfree((void*)(uintptr_t)cr3_phys);
}

void kmalloc_init(void) {
    uint64_t heap_base = 0x900000;
    uint64_t heap_size = 0x1000000;
    int heap_region_found = 0;

    for (uint32_t i = 0; i < global_mmap.count; i++) {
        struct mm_map_entry *ent = &global_mmap.entries[i];
        uint64_t base = ((uint64_t)ent->base_high << 32) | ent->base_low;
        uint64_t length = ((uint64_t)ent->length_high << 32) | ent->length_low;
        uint64_t end = base + length;
        if (ent->type == 1 && base <= heap_base && end >= (heap_base + heap_size)) {
            heap_region_found = 1;
            break;
        }
    }

    if (!heap_region_found) {
        for (uint32_t i = 0; i < global_mmap.count; i++) {
            struct mm_map_entry *ent = &global_mmap.entries[i];
            uint64_t base = ((uint64_t)ent->base_high << 32) | ent->base_low;
            uint64_t length = ((uint64_t)ent->length_high << 32) | ent->length_low;
            if (ent->type == 1 && base >= 0x900000 && length >= heap_size) {
                heap_base = base;
                heap_region_found = 1;
                break;
            }
        }
    }

    heap_start = (void*)(uintptr_t)heap_base;
    heap_end = (void*)((uintptr_t)heap_base + heap_size);

    block_header_t* first = (block_header_t*)heap_start;
    first->magic = HEAP_MAGIC;
    first->size = (size_t)heap_end - (size_t)heap_start - sizeof(block_header_t);
    first->is_free = 1;
    first->next = NULL;
    first->prev = NULL;

    free_list = first;
    total_heap_size = block_total_size(first);
    used_heap_size = 0;
}

void* kmalloc(size_t size) {
    if (size == 0 || !heap_start) return NULL;

    size = align_size(size);
    block_header_t* block = find_free_block(size);
    if (!block) return NULL;

    remove_from_list(block);
    split_block(block, size);

    block->is_free = 0;
    used_heap_size += block_total_size(block);

    return get_block_payload(block);
}

void* kcalloc(size_t num, size_t size) {
    if (num && SIZE_MAX / num < size) return NULL;

    size_t total = num * size;
    void* p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    block_header_t* block = get_block_from_payload(ptr);
    if (!block || block->magic != HEAP_MAGIC) {
#ifdef ENABLE_VGA
        vga_print("Heap corruption detected!\n");
#endif
        return NULL;
    }

    size = align_size(size);

    if (block->size >= size) {
        block_header_t* new_free = split_block(block, size);
        if (new_free) {
            used_heap_size -= block_total_size(new_free);
        }
        return ptr;
    }

    void* newp = kmalloc(size);
    if (!newp) return NULL;

    memcpy(newp, ptr, block->size);
    kfree(ptr);
    return newp;
}

void kfree(void* ptr) {
    if (!ptr || !heap_start) return;

    block_header_t* block = get_block_from_payload(ptr);
    if (!block || block->magic != HEAP_MAGIC) {
#ifdef ENABLE_VGA
        vga_print("Invalid kfree() call!\n");
#endif
        return;
    }

    if (block->is_free) {
#ifdef ENABLE_VGA
        vga_print("Double free detected!\n");
#endif
        return;
    }

    block->is_free = 1;
    used_heap_size -= block_total_size(block);

    insert_into_list_sorted(block);
    merge_free_blocks(block);
}

void* valloc(size_t size) {
    return kmalloc(size);
}

void* valloc_aligned(size_t size, size_t alignment) {
    if (alignment < ALIGNMENT) alignment = ALIGNMENT;
    
    size_t total = size + alignment + sizeof(void*);
    void* raw = kmalloc(total);
    if (!raw) return NULL;

    uintptr_t addr = (uintptr_t)raw + sizeof(void*);
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    ((void**)aligned)[-1] = raw;
    return (void*)aligned;
}

void vfree(void* ptr) {
    if (!ptr) return;
    void* raw = ((void**)ptr)[-1];
    kfree(raw);
}

size_t get_total_heap(void) { return total_heap_size; }
size_t get_used_heap(void) { return used_heap_size; }
size_t get_free_heap(void) { return total_heap_size - used_heap_size; }
