.DEFAULT_GOAL := all
include ../common.mk

TARGETS := libhgui.so libhstd.so libdep.so libdemo.so
STAGE_INITRAMFS_DIR := $(BUILD_DIR)/initramfs/lib
STAGE_ROOTFS_DIR := $(BUILD_DIR)/rootfs/lib

PIC_CFLAGS := $(CFLAGS) -fPIC
SO_LDFLAGS := -m elf_i386 -shared -nostdlib

all: $(TARGETS)

libhgui.so: $(BUILD_DIR)/hgui.o
	$(LD) $(SO_LDFLAGS) $^ -o $@
	@mkdir -p $(STAGE_INITRAMFS_DIR) $(STAGE_ROOTFS_DIR)
	@cp $@ $(STAGE_INITRAMFS_DIR)/
	@cp $@ $(STAGE_ROOTFS_DIR)/

libhstd.so: $(BUILD_DIR)/hstd.o
	$(LD) $(SO_LDFLAGS) $^ -o $@
	@mkdir -p $(STAGE_INITRAMFS_DIR) $(STAGE_ROOTFS_DIR)
	@cp $@ $(STAGE_INITRAMFS_DIR)/
	@cp $@ $(STAGE_ROOTFS_DIR)/

libdep.so: $(BUILD_DIR)/dep.o
	$(LD) $(SO_LDFLAGS) $^ -o $@
	@mkdir -p $(STAGE_INITRAMFS_DIR) $(STAGE_ROOTFS_DIR)
	@cp $@ $(STAGE_INITRAMFS_DIR)/
	@cp $@ $(STAGE_ROOTFS_DIR)/

libdemo.so: $(BUILD_DIR)/demo.o libdep.so
	$(LD) $(SO_LDFLAGS) $(BUILD_DIR)/demo.o -L. -ldep -o $@
	@mkdir -p $(STAGE_INITRAMFS_DIR) $(STAGE_ROOTFS_DIR)
	@cp $@ $(STAGE_INITRAMFS_DIR)/
	@cp $@ $(STAGE_ROOTFS_DIR)/

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(PIC_CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGETS)

.PHONY: all clean
