BUILD_DIR = build

SYSTEM_IMG = $(BUILD_DIR)/system.img

.PHONY: all clean run

all: $(SYSTEM_IMG)

$(SYSTEM_IMG): $(BUILD_DIR)/bootloader $(BUILD_DIR)/kernel | $(BUILD_DIR)
	@echo "DD    $@"
	@dd if=/dev/zero of=$@ bs=512 count=2880 2>/dev/null
	@dd if=$(BUILD_DIR)/bootloader.bin of=$@ conv=notrunc 2>/dev/null
	@dd if=$(BUILD_DIR)/kernel.bin of=$@ bs=512 seek=6 conv=notrunc 2>/dev/null
	@echo "  DONE  Образ создан: $@"

$(BUILD_DIR)/bootloader: | $(BUILD_DIR)
	@echo "MAKE  bootloader"
	@$(MAKE) -C bootloader
	@cp bootloader/build/bootloader.bin $@.bin

$(BUILD_DIR)/kernel: | $(BUILD_DIR)
	@echo "MAKE  kernel"
	@$(MAKE) -C kernel
	@cp kernel/build/kernel.bin $@.bin

$(BUILD_DIR):
	@mkdir -p $@

run: $(SYSTEM_IMG)
	@echo "QEMU  $@"
	@qemu-system-i386 -drive format=raw,file=$(SYSTEM_IMG) -serial stdio

debug: $(SYSTEM_IMG)
	@echo "QEMU  $@"
	@echo "Attach to system: target remote localhost:1234"
	@qemu-system-i386 -drive format=raw,file=$(SYSTEM_IMG) -S -s

clean:
	@echo "CLEAN"
	@$(MAKE) -C bootloader clean
	@$(MAKE) -C kernel clean
	@rm -rf $(BUILD_DIR)