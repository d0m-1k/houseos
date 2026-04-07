ARCH = i386

ASM = nasm
CC = gcc
LD = ld
OBJCP = objcopy

BUILD_DIR = build
INCLUDE_DIR = include
CONFIG_XHCI ?= n
CONFIG_USBKBD ?= n
CONFIG_PS2_KEYBOARD ?= y
CONFIG_PS2_MOUSE ?= y
CONFIG_GSHELL ?= n
CONFIG_KERNEL_FS_DEVFS ?= y
CONFIG_KERNEL_FS_PROCFS ?= y
CONFIG_KERNEL_FS_FAT32 ?= y
CONFIG_GFX_BACKEND ?= vga
CONFIG_VESA_ROTATION_DEG ?= 0
CONFIG_DEBUG_KERNEL_SERIAL_LOG ?= y

ASMFLAGS = -f elf32
CFLAGS = -std=gnu99 -m32 -ffreestanding -fno-stack-protector -Os -Wall -Wextra \
	-mno-sse -mno-sse2 -mno-mmx -mno-80387 \
	-I$(INCLUDE_DIR) -I. -Iarch/$(ARCH)/include
ifeq ($(CONFIG_PS2_KEYBOARD),y)
CFLAGS += -DCONFIG_PS2_KEYBOARD=1
else
CFLAGS += -DCONFIG_PS2_KEYBOARD=0
endif
ifeq ($(CONFIG_PS2_MOUSE),y)
CFLAGS += -DCONFIG_PS2_MOUSE=1
else
CFLAGS += -DCONFIG_PS2_MOUSE=0
endif
ifeq ($(CONFIG_DEBUG_KERNEL_SERIAL_LOG),y)
CFLAGS += -DCONFIG_DEBUG_KERNEL_SERIAL_LOG=1
else
CFLAGS += -DCONFIG_DEBUG_KERNEL_SERIAL_LOG=0
endif
ifeq ($(CONFIG_KERNEL_FS_DEVFS),y)
CFLAGS += -DCONFIG_KERNEL_FS_DEVFS=1
else
CFLAGS += -DCONFIG_KERNEL_FS_DEVFS=0
endif
ifeq ($(CONFIG_KERNEL_FS_PROCFS),y)
CFLAGS += -DCONFIG_KERNEL_FS_PROCFS=1
else
CFLAGS += -DCONFIG_KERNEL_FS_PROCFS=0
endif
ifeq ($(CONFIG_KERNEL_FS_FAT32),y)
CFLAGS += -DCONFIG_KERNEL_FS_FAT32=1
else
CFLAGS += -DCONFIG_KERNEL_FS_FAT32=0
endif
ifeq ($(CONFIG_GFX_BACKEND),vesa)
CFLAGS += -DCONFIG_GFX_BACKEND_VESA=1 -DCONFIG_GFX_BACKEND_VGA=0
else
CFLAGS += -DCONFIG_GFX_BACKEND_VESA=0 -DCONFIG_GFX_BACKEND_VGA=1
endif
CFLAGS += -DCONFIG_VESA_ROTATION_DEG=$(CONFIG_VESA_ROTATION_DEG)
LDFLAGS = -m elf_i386 -T linker.ld -no-warn-rwx-segments -nostdlib
OBJCPFLAGS = -O binary

C_SOURCES = $(shell find . -name '*.c' -not -path "./$(BUILD_DIR)/*")

ifeq ($(CONFIG_XHCI),n)
C_SOURCES := $(filter-out ./drivers/xhci.c,$(C_SOURCES))
endif

ifeq ($(CONFIG_USBKBD),n)
C_SOURCES := $(filter-out ./drivers/usbkbd.c,$(C_SOURCES))
endif

ifeq ($(CONFIG_GSHELL),n)
C_SOURCES := $(filter-out ./progs/gshell.c ./progs/applications.c ./progs/system_info.c ./progs/panel.c,$(C_SOURCES))
endif
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
