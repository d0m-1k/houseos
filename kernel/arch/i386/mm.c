#include <asm/mm.h>
#include <drivers/vga.h>
#include <string.h>

static void* heap_start = NULL;
static void* heap_end = NULL;
static block_header_t* free_list = NULL;
static size_t total_heap_size = 0;
static size_t used_heap_size = 0;

void mm_init() {
    volatile struct mm_map_entry *entry = (volatile struct mm_map_entry *)MM_MAP_ADDRESS;
    static struct mm_map map;
    map.count = 0;
    
    for (int i = 0; i < MM_MAX_ENTRIES; i++) {
        uint64_t base = ((uint64_t)entry[i].base_high << 32) | entry[i].base_low;
        uint64_t length = ((uint64_t)entry[i].length_high << 32) | entry[i].length_low;
        
        if (base == 0 && length == 0) break;
        
        if (map.count < MM_MAX_ENTRIES) {
            map.entries[map.count] = entry[i];
            map.count++;
        }
    }
}

static size_t align_size(size_t size) {
    return (size + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);
}

static void* get_block_payload(block_header_t* block) {
    return (void*)((uint8_t*)block + sizeof(block_header_t));
}

static block_header_t* get_block_from_payload(void* ptr) {
    return (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
}

static block_header_t* find_free_block(size_t size) {
    block_header_t* current = free_list;
    
    while (current) {
        if (current->is_free && current->size >= size) return current;
        current = current->next;
    }
    
    return NULL;
}

static void split_block(block_header_t* block, size_t size) {
    size_t remaining_size = block->size - size - sizeof(block_header_t);
    
    if (remaining_size >= sizeof(block_header_t) + MIN_BLOCK_SIZE) {
        block_header_t* new_block = (block_header_t*)((uint8_t*)block + size);
        
        new_block->magic = HEAP_MAGIC;
        new_block->size = remaining_size;
        new_block->is_free = 1;
        new_block->prev = block;
        new_block->next = block->next;
        
        if (block->next) block->next->prev = new_block;
        
        block->next = new_block;
        block->size = size;
        
        new_block->next = free_list;
        free_list = new_block;
    }
}

static void merge_free_blocks(block_header_t* block) {
    if (block->prev && block->prev->is_free) {
        block->prev->size += block->size + sizeof(block_header_t);
        block->prev->next = block->next;
        
        if (block->next) block->next->prev = block->prev;
        
        block = block->prev;
    }
    
    if (block->next && block->next->is_free) {
        block->size += block->next->size + sizeof(block_header_t);
        block->next = block->next->next;
        
        if (block->next) block->next->prev = block;
    }
}

void kmalloc_init() {
    struct mm_map map;
    
    heap_start = (void*)0x200000;
    heap_end = (void*)((uint32_t)heap_start + 0x400000); // 4 МБ
    
    block_header_t* first_block = (block_header_t*)heap_start;
    first_block->magic = HEAP_MAGIC;
    first_block->size = (uint32_t)heap_end - (uint32_t)heap_start - sizeof(block_header_t);
    first_block->is_free = 1;
    first_block->next = NULL;
    first_block->prev = NULL;
    
    free_list = first_block;
    total_heap_size = first_block->size;
    used_heap_size = 0;
}

void* kmalloc(size_t size) {
    if (size == 0 || heap_start == NULL) return NULL;
    
    size_t total_size = align_size(size) + sizeof(block_header_t);
    
    block_header_t* block = find_free_block(total_size);
    
    if (!block) return NULL;
    
    if (block == free_list) free_list = block->next;
    if (block->prev) block->prev->next = block->next;
    if (block->next) block->next->prev = block->prev;
    
    split_block(block, total_size);
    
    block->is_free = 0;
    used_heap_size += block->size;
    
    return get_block_payload(block);
}

void* kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = kmalloc(total);
    
    if (ptr) memset(ptr, 0, total);
    
    return ptr;
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    block_header_t* block = get_block_from_payload(ptr);
    
    if (block->magic != HEAP_MAGIC) {
        vga_print("Heap corruption detected!\n");
        return NULL;
    }
    
    if (block->size >= size + sizeof(block_header_t)) {
        split_block(block, align_size(size) + sizeof(block_header_t));
        return ptr;
    }
    
    void* new_ptr = kmalloc(size);
    if (!new_ptr) return NULL;
    
    size_t copy_size = block->size - sizeof(block_header_t);
    if (size < copy_size) copy_size = size;
    
    memcpy(new_ptr, ptr, copy_size);
    
    kfree(ptr);
    
    return new_ptr;
}

void kfree(void* ptr) {
    if (!ptr || heap_start == NULL) return;
    
    block_header_t* block = get_block_from_payload(ptr);
    
    if (block->magic != HEAP_MAGIC) {
        vga_print("Invalid kfree() call!\n");
        return;
    }
    
    if (block->is_free) {
        vga_print("Double free detected!\n");
        return;
    }
    
    block->is_free = 1;
    used_heap_size -= block->size;
    
    block->next = free_list;
    block->prev = NULL;
    
    if (free_list) free_list->prev = block;
    
    free_list = block;
    
    merge_free_blocks(block);
}

void* valloc(size_t size) {
    // Простая реализация valloc без выравнивания
    return kmalloc(size);
}

void* valloc_aligned(size_t size, size_t alignment) {
    // Простая реализация - просто выделяем память
    // В реальной системе нужно учитывать выравнивание
    return kmalloc(size);
}

void vfree(void* ptr) {
    kfree(ptr);
}

size_t get_total_heap() {
    return total_heap_size;
}

size_t get_used_heap() {
    return used_heap_size;
}

size_t get_free_heap() {
    return total_heap_size - used_heap_size;
}

// Добавляем отладочные функции
void heap_debug() {
    vga_print("Heap debug:\n");
    char buffer[32];
    
    itoa((uintptr_t)heap_start, buffer, 16);
    vga_print("  Heap start: 0x");
    vga_print(buffer);
    vga_print("\n");
    
    itoa((uintptr_t)heap_end, buffer, 16);
    vga_print("  Heap end: 0x");
    vga_print(buffer);
    vga_print("\n");
    
    itoa(total_heap_size, buffer, 10);
    vga_print("  Total size: ");
    vga_print(buffer);
    vga_print(" bytes\n");
    
    itoa(used_heap_size, buffer, 10);
    vga_print("  Used: ");
    vga_print(buffer);
    vga_print(" bytes\n");
    
    itoa(get_free_heap(), buffer, 10);
    vga_print("  Free: ");
    vga_print(buffer);
    vga_print(" bytes\n");
    
    vga_print("  Free list: ");
    block_header_t* current = free_list;
    while (current) {
        itoa((uintptr_t)current, buffer, 16);
        vga_print("0x");
        vga_print(buffer);
        vga_print("->");
        current = current->next;
    }
    vga_print("NULL\n");
}