.DEFAULT_GOAL := all
include ../common.mk

TARGET := dyncalc.elf
OBJS := $(BUILD_DIR)/crt0.o $(BUILD_DIR)/main.o
STAGE_INITRAMFS_DIR := $(BUILD_DIR)/initramfs/bin
STAGE_ROOTFS_DIR := $(BUILD_DIR)/rootfs/bin
LDFLAGS_DYNCALC := -m elf_i386 -T user_dynlink.ld -nostdlib --dynamic-linker /lib/ld-house.so --allow-shlib-undefined

all: $(TARGET)

$(TARGET): $(OBJS) $(STDLIB_A) ../libs/libdemo.so
	$(LD) $(LDFLAGS_DYNCALC) $(OBJS) $(STDLIB_A) -L../libs -ldemo -o $@
	@mkdir -p $(STAGE_INITRAMFS_DIR) $(STAGE_ROOTFS_DIR)
	@cp $@ $(STAGE_INITRAMFS_DIR)/dyncalc
	@cp $@ $(STAGE_ROOTFS_DIR)/dyncalc

../libs/libdemo.so:
	@$(MAKE) -C ../libs all

$(BUILD_DIR)/crt0.o: ../stdlib/crt0.asm
	@mkdir -p $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/main.o: main.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean
