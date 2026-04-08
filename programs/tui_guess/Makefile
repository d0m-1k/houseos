.DEFAULT_GOAL := all
include ../common.mk

TARGET := tui_guess.elf
OBJS := $(BUILD_DIR)/crt0.o $(BUILD_DIR)/main.o
STAGE_INITRAMFS_DIR := $(BUILD_DIR)/initramfs/bin
STAGE_ROOTFS_DIR := $(BUILD_DIR)/rootfs/bin

all: $(TARGET)

$(TARGET): $(OBJS) $(STDLIB_A)
	$(LD) $(LDFLAGS) $(OBJS) $(STDLIB_A) -o $@
	@mkdir -p $(STAGE_INITRAMFS_DIR) $(STAGE_ROOTFS_DIR)
	@cp $@ $(STAGE_INITRAMFS_DIR)/tui_guess
	@cp $@ $(STAGE_ROOTFS_DIR)/tui_guess

$(BUILD_DIR)/crt0.o: ../stdlib/crt0.asm
	@mkdir -p $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/main.o: main.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean
