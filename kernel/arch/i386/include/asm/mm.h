#pragma once

#include <stdint.h>
#include <stddef.h>

#define MM_MAP_ADDRESS 0x5000
#define MM_MAX_ENTRIES 64

#define HEAP_MAGIC 0xDEADBEEF
#define MIN_BLOCK_SIZE 16
#define ALIGNMENT 8

#define USER_VADDR_BASE 0x00400000u
#define USER_VADDR_SIZE 0x00400000u
#define USER_STACK_TOP  (USER_VADDR_BASE + USER_VADDR_SIZE - 16u)

#define USER_SLOT_BASE_PHYS 0x02000000u
#define USER_SLOT_SIZE_PHYS 0x00400000u
#define USER_SLOT_COUNT 12

struct mm_map_entry {
    uint32_t base_low;
    uint32_t base_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t type;
} __attribute__((packed));

struct mm_map {
    uint32_t count;
    struct mm_map_entry entries[MM_MAX_ENTRIES];
};

typedef struct block_header {
    uint32_t magic;
    size_t size;
    uint8_t is_free;
    struct block_header* next;
    struct block_header* prev;
} block_header_t;

extern struct mm_map global_mmap;

void mm_init(void);
void kmalloc_init(void);
void paging_init(void);
uint32_t mm_kernel_cr3(void);
void mm_switch_cr3(uint32_t cr3_phys);

int mm_user_slot_alloc(uint32_t *slot_idx_out, uint32_t *phys_base_out);
void mm_user_slot_free(uint32_t slot_idx);
uint32_t mm_user_cr3_create(uint32_t user_phys_base);
void mm_user_cr3_destroy(uint32_t cr3_phys);

void* kmalloc(size_t size);
void* kcalloc(size_t num, size_t size);
void* krealloc(void* ptr, size_t size);
void kfree(void* ptr);

void* valloc(size_t size);
void* valloc_aligned(size_t size, size_t alignment);
void vfree(void* ptr);

size_t get_total_heap(void);
size_t get_used_heap(void);
size_t get_free_heap(void);
