#pragma once

#include <stdint.h>

#define MM_MAP_ADDRESS 0x5000
#define MM_MAX_ENTRIES 64

#define HEAP_MAGIC 0xDEADBEEF
#define MIN_BLOCK_SIZE 16
#define ALIGNMENT 8

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

void mm_init(void);
void kmalloc_init(void);

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

void heap_debug(void);