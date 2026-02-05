ARCH = i386

ASM = nasm
CC = gcc
LD = ld
OBJCP = objcopy

BUILD_DIR = build
INCLUDE_DIR = include

ASMFLAGS = -f elf32
CFLAGS = -std=gnu99 -m32 -ffreestanding -fno-stack-protector -O0 -Wall -Wextra \
	-mno-sse -mno-sse2 -mno-mmx -mno-80387 \
	-I$(INCLUDE_DIR) -I. -Iarch/$(ARCH)/include
LDFLAGS = -m elf_i386 -T linker.ld -no-warn-rwx-segments -nostdlib
OBJCPFLAGS = -O binary

C_SOURCES = $(shell find . -name '*.c' -not -path "./$(BUILD_DIR)/*")
ASM_SOURCES = $(shell find . -name '*.asm' -not -path "./$(BUILD_DIR)/*")

C_OBJECTS = $(patsubst ./%.c, $(BUILD_DIR)/%.o, $(C_SOURCES))
ASM_OBJECTS = $(patsubst ./%.asm, $(BUILD_DIR)/%.o, $(ASM_SOURCES))

OBJECTS = $(C_OBJECTS) $(ASM_OBJECTS)

KERNEL_BINARY = $(BUILD_DIR)/kernel.bin
KERNEL_ELF = $(BUILD_DIR)/kernel.elf

.PHONY: all clean

all: $(KERNEL_BINARY)

$(KERNEL_BINARY): $(OBJECTS) | linker.ld $(BUILD_DIR)
	@echo "  LD    $@"
	@$(LD) $(LDFLAGS) $^ -o $(KERNEL_ELF)
	@echo "  OBJCP $@"
	@$(OBJCP) $(OBJCPFLAGS) $(KERNEL_ELF) $@

$(BUILD_DIR)/%.o: %.asm | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@echo "  ASM   $@"
	@$(ASM) $(ASMFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@echo "  CC    $@"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

clean:
	@echo "  RM    $(BUILD_DIR)"
	@rm -rf $(BUILD_DIR)