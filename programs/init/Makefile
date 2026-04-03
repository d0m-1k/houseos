.DEFAULT_GOAL := all
include ../common.mk

TARGET := init.elf
OBJS := $(BUILD_DIR)/crt0.o $(BUILD_DIR)/main.o
STAGE_INITRAMFS_DIR := $(BUILD_DIR)/initramfs
STAGE_ROOTFS_DIR := $(BUILD_DIR)/rootfs

all: $(TARGET)

$(TARGET): $(OBJS) $(STDLIB_A)
	$(LD) $(LDFLAGS) $(OBJS) $(STDLIB_A) -o $@
	@mkdir -p $(STAGE_INITRAMFS_DIR) $(STAGE_ROOTFS_DIR) \
		$(STAGE_INITRAMFS_DIR)/etc $(STAGE_ROOTFS_DIR)/etc
	@rm -f $(STAGE_INITRAMFS_DIR)/etc/fbtab $(STAGE_ROOTFS_DIR)/etc/fbtab
	@cp $@ $(STAGE_INITRAMFS_DIR)/
	@cp $@ $(STAGE_ROOTFS_DIR)/
	@cp init.conf $(STAGE_INITRAMFS_DIR)/etc/init.conf
	@cp fstab $(STAGE_INITRAMFS_DIR)/etc/fstab
	@cp init.conf $(STAGE_ROOTFS_DIR)/etc/init.conf
	@cp fstab $(STAGE_ROOTFS_DIR)/etc/fstab

$(BUILD_DIR)/crt0.o: ../stdlib/crt0.asm
	@mkdir -p $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/main.o: main.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean
