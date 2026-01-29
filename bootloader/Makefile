ASM = nasm

BUILD_DIR = build
SOURCE_DIR = src

MBR_ASMFLAGS = -I $(SOURCE_DIR) -fbin
ST2_ASMFLAGS = -I $(SOURCE_DIR) -fbin

MBR_SOURCES = $(SOURCE_DIR)/mbr.asm $(SOURCE_DIR)/print.asm
ST2_SOURCES = $(SOURCE_DIR)/boot.asm ${shell find $(SOURCE_DIR) -type f -name "*.asm" ! -name "mbr.asm" ! -name "boot.asm"}

MBR_BINARY = $(BUILD_DIR)/mbr.bin
ST2_BINARY = $(BUILD_DIR)/st2.bin
BOOTLOADER_BINARY = $(BUILD_DIR)/bootloader.bin

.PHONY: all $(BOOTLOADER_BINARY) clean

all: $(BOOTLOADER_BINARY)

$(BOOTLOADER_BINARY): $(MBR_BINARY) $(ST2_BINARY) | $(BUILD_DIR)
	@echo "  DD    $@"
	@dd if=/dev/zero of=$@ bs=512 count=5 2>/dev/null
	@dd if=$(MBR_BINARY) of=$@ conv=notrunc 2>/dev/null
	@dd if=$(ST2_BINARY) of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	@cat $^ > $@

$(MBR_BINARY): $(MBR_SOURCES) | $(BUILD_DIR)
	@echo "  ASM   $@"
	@$(ASM) $(MBR_ASMFLAGS) $< -o $@

$(ST2_BINARY): $(ST2_SOURCES) | $(BUILD_DIR)
	@echo "  ASM   $@"
	@$(ASM) $(ST2_ASMFLAGS) $< -o $@

$(BUILD_DIR):
	@echo "  MKDIR $@"
	@mkdir -p $@

clean:
	@echo "  RM    $(BUILD_DIR)"
	@rm -rf $(BUILD_DIR)