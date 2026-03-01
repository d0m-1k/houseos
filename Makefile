BUILD_DIR = build

SYSTEM_IMG = $(BUILD_DIR)/system.img
INITRAMFS_MAX_BYTES = 65536
KERNEL_MAX_BYTES = 131072

.PHONY: all clean run

all: $(SYSTEM_IMG)

$(SYSTEM_IMG): $(BUILD_DIR)/bootloader $(BUILD_DIR)/programs $(BUILD_DIR)/initramfs $(BUILD_DIR)/kernel | $(BUILD_DIR)
	@echo "DD    $@"
	@init_sz=$$(wc -c < $(BUILD_DIR)/initramfs.bin); \
	if [ $$init_sz -gt $(INITRAMFS_MAX_BYTES) ]; then \
		echo "ERROR initramfs too big: $$init_sz > $(INITRAMFS_MAX_BYTES)"; \
		exit 1; \
	fi
	@kernel_sz=$$(wc -c < $(BUILD_DIR)/kernel.bin); \
	if [ $$kernel_sz -gt $(KERNEL_MAX_BYTES) ]; then \
		echo "ERROR kernel too big: $$kernel_sz > $(KERNEL_MAX_BYTES)"; \
		exit 1; \
	fi
	@dd if=/dev/zero of=$@ bs=512 count=2880 2>/dev/null
	@dd if=$(BUILD_DIR)/bootloader.bin of=$@ conv=notrunc 2>/dev/null
	@dd if=$(BUILD_DIR)/initramfs.bin of=$@ bs=512 seek=6 conv=notrunc 2>/dev/null
	@dd if=$(BUILD_DIR)/kernel.bin of=$@ bs=512 seek=134 conv=notrunc 2>/dev/null
	@echo "DONE  Образ создан: $@"

$(BUILD_DIR)/bootloader: | $(BUILD_DIR)
	@echo "MAKE  bootloader"
	@$(MAKE) -C bootloader
	@cp bootloader/build/bootloader.bin $@.bin

$(BUILD_DIR)/programs: | $(BUILD_DIR)
	@echo "MAKE  programs"
	@$(MAKE) -C programs
	@mkdir -p initramfs/data/bin
	@cp programs/init/init.elf initramfs/data/bin/init
	@cp programs/shell/shell.elf initramfs/data/bin/shell
	@cp programs/fb_demo/fb_demo.elf initramfs/data/bin/fb_demo
	@touch $@

$(BUILD_DIR)/initramfs: $(BUILD_DIR)/programs | $(BUILD_DIR)
	@echo "MAKE  initramfs"
	@$(MAKE) -C initramfs clean
	@$(MAKE) -C initramfs all
	@cp initramfs/initramfs.bin $@.bin

$(BUILD_DIR)/kernel: | $(BUILD_DIR)
	@echo "MAKE  kernel"
	@$(MAKE) -C kernel
	@cp kernel/build/kernel.bin $@.bin

$(BUILD_DIR):
	@mkdir -p $@

run: $(SYSTEM_IMG)
	@echo "QEMU  $@"
	@qemu-system-i386 -drive format=raw,file=$(SYSTEM_IMG) -serial stdio -m 4G

debug: $(SYSTEM_IMG)
	@echo "QEMU  $@"
	@echo "Attach to system: target remote localhost:1234"
	@qemu-system-i386 -drive format=raw,file=$(SYSTEM_IMG) -S -s -m 4G

clean:
	@echo "CLEAN"
	@$(MAKE) -C bootloader clean
	@$(MAKE) -C programs clean
	@$(MAKE) -C kernel clean
	@rm -rf $(BUILD_DIR)
