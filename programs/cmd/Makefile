.DEFAULT_GOAL := all
include ../common.mk

TARGET := cmd.elf
C_SRCS := $(filter-out $(BUILD_DIR)/%,$(wildcard *.c))
OBJS := $(BUILD_DIR)/crt0.o $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SRCS))
STAGE_INITRAMFS_DIR := $(BUILD_DIR)/initramfs
STAGE_ROOTFS_DIR := $(BUILD_DIR)/rootfs

all: $(TARGET)

$(TARGET): $(OBJS) $(STDLIB_A)
	$(LD) $(LDFLAGS) $(OBJS) $(STDLIB_A) -o $@
	@mkdir -p $(STAGE_INITRAMFS_DIR) $(STAGE_ROOTFS_DIR)
	@cp $@ $(STAGE_INITRAMFS_DIR)/
	@cp $@ $(STAGE_ROOTFS_DIR)/

$(BUILD_DIR)/crt0.o: ../stdlib/crt0.asm
	@mkdir -p $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean
