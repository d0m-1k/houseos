.DEFAULT_GOAL := all
include ../common.mk

TARGET := ldso.elf
OBJS := $(BUILD_DIR)/crt0.o $(BUILD_DIR)/main.o
STAGE_INITRAMFS_DIR := $(BUILD_DIR)/initramfs/lib
STAGE_ROOTFS_DIR := $(BUILD_DIR)/rootfs/lib
LDFLAGS_LDSO := -m elf_i386 -T user.ld -nostdlib

all: $(TARGET)

$(TARGET): $(OBJS) $(STDLIB_A)
	$(LD) $(LDFLAGS_LDSO) $(OBJS) $(STDLIB_A) -o $@
	@mkdir -p $(STAGE_INITRAMFS_DIR) $(STAGE_ROOTFS_DIR)
	@cp $@ $(STAGE_INITRAMFS_DIR)/ld-house.so
	@cp $@ $(STAGE_ROOTFS_DIR)/ld-house.so
	@cp $@ $(STAGE_INITRAMFS_DIR)/ld.so
	@cp $@ $(STAGE_ROOTFS_DIR)/ld.so
	@cp $@ $(STAGE_INITRAMFS_DIR)/ld-linux.so.2
	@cp $@ $(STAGE_ROOTFS_DIR)/ld-linux.so.2

$(BUILD_DIR)/crt0.o: ../stdlib/crt0.asm
	@mkdir -p $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/main.o: main.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean
