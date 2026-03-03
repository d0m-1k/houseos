BUILD_DIR = build
ASM = nasm

SYSTEM_IMG = $(BUILD_DIR)/system.img
IMG_SECTORS = 2880
STAGE2_SECTORS = 4
CFG_SECTOR = 1
STAGE2_START = 2
PAYLOAD_START = $(shell echo $$(( $(STAGE2_START) + $(STAGE2_SECTORS) )))

.PHONY: all clean run

all: $(SYSTEM_IMG)

$(SYSTEM_IMG): $(BUILD_DIR)/bootloader $(BUILD_DIR)/programs $(BUILD_DIR)/initramfs $(BUILD_DIR)/kernel | $(BUILD_DIR)
	@echo "DD    $@"
	@stage2_sz=$$(wc -c < $(BUILD_DIR)/st2.bin); \
	if [ $$stage2_sz -gt $$((512 * $(STAGE2_SECTORS))) ]; then \
		echo "ERROR stage2 too big: $$stage2_sz > $$((512 * $(STAGE2_SECTORS)))"; \
		exit 1; \
	fi; \
	kernel_sz=$$(wc -c < $(BUILD_DIR)/kernel.bin); \
	init_sz=$$(wc -c < $(BUILD_DIR)/initramfs.bin); \
	kernel_sec=$$(( (kernel_sz + 511) / 512 )); \
	init_sec=$$(( (init_sz + 511) / 512 )); \
	kernel_lba=$(PAYLOAD_START); \
	init_lba=$$((kernel_lba + kernel_sec)); \
	p1_start=$(CFG_SECTOR); \
	p1_size=$$((1 + $(STAGE2_SECTORS) + kernel_sec + init_sec)); \
	p2_start=$$((p1_start + p1_size)); \
	p2_size=$$(( $(IMG_SECTORS) - p2_start )); \
	if [ $$p2_size -le 0 ]; then \
		echo "ERROR image overflow: payload does not fit into $(IMG_SECTORS) sectors"; \
		exit 1; \
	fi; \
	$(ASM) -f bin -I bootloader/src \
		-D CFG_KERNEL_SIZE=$$kernel_sz \
		-D CFG_KERNEL_LBA=$$kernel_lba \
		-D CFG_KERNEL_ADDR=0x00010000 \
		-D CFG_INITRAMFS_SIZE=$$init_sz \
		-D CFG_INITRAMFS_LBA=$$init_lba \
		-D CFG_INITRAMFS_ADDR=0x00080000 \
		-D CFG_MEMMAP_ADDR=0x00005000 \
		-D CFG_VESA_MODE=0x0118 \
		-D CFG_VESA_INFO_ADDR=0x00009000 \
		-D CFG_VESA_MODE_INFO_ADDR=0x00009100 \
		-D CFG_STAGE2_LBA=$(STAGE2_START) \
		-D CFG_STAGE2_SECTORS=$(STAGE2_SECTORS) \
		-D CFG_FLAGS=1 \
		-D CFG_ROOTFS_LBA=$$p2_start \
		-D CFG_ROOTFS_SIZE=$$((p2_size * 512)) \
		bootloader/src/bootcfg.asm -o $(BUILD_DIR)/bootcfg.bin; \
	dd if=/dev/zero of=$@ bs=512 count=$(IMG_SECTORS) 2>/dev/null; \
	dd if=$(BUILD_DIR)/mbr.bin of=$@ conv=notrunc 2>/dev/null; \
	dd if=$(BUILD_DIR)/bootcfg.bin of=$@ bs=512 seek=$(CFG_SECTOR) conv=notrunc 2>/dev/null; \
	dd if=$(BUILD_DIR)/st2.bin of=$@ bs=512 seek=$(STAGE2_START) conv=notrunc 2>/dev/null; \
	dd if=$(BUILD_DIR)/kernel.bin of=$@ bs=512 seek=$$kernel_lba conv=notrunc 2>/dev/null; \
	dd if=$(BUILD_DIR)/initramfs.bin of=$@ bs=512 seek=$$init_lba conv=notrunc 2>/dev/null; \
	dd if=$(BUILD_DIR)/initramfs.bin of=$@ bs=512 seek=$$p2_start conv=notrunc 2>/dev/null; \
	printf 'label: dos\nunit: sectors\n\nstart=%s, size=%s, type=83, bootable\nstart=%s, size=%s, type=83\n' \
	  $$p1_start $$p1_size $$p2_start $$p2_size | \
	  sfdisk --no-reread --no-tell-kernel $@ >/dev/null
	@echo "DONE  Образ создан: $@"

$(BUILD_DIR)/bootloader: | $(BUILD_DIR)
	@echo "MAKE  bootloader"
	@$(MAKE) -C bootloader
	@cp bootloader/build/mbr.bin $(BUILD_DIR)/mbr.bin
	@cp bootloader/build/st2.bin $(BUILD_DIR)/st2.bin
	@touch $@

$(BUILD_DIR)/programs: | $(BUILD_DIR)
	@echo "MAKE  programs"
	@$(MAKE) -C programs
	@mkdir -p initramfs/data/bin
	@cp programs/init/init.elf initramfs/data/bin/init
	@cp programs/shell/sh.elf initramfs/data/bin/sh
	@cp programs/cmd/cmd.elf initramfs/data/bin/cmd
	@rm -f initramfs/data/bin/echo initramfs/data/bin/pwd initramfs/data/bin/ls initramfs/data/bin/cat \
		initramfs/data/bin/mkdir initramfs/data/bin/mkfifo initramfs/data/bin/mksock initramfs/data/bin/touch \
		initramfs/data/bin/rm initramfs/data/bin/rmdir initramfs/data/bin/tee initramfs/data/bin/chvt \
		initramfs/data/bin/fbinfo initramfs/data/bin/ttyinfo initramfs/data/bin/kbdinfo initramfs/data/bin/mouseinfo \
		initramfs/data/bin/reboot initramfs/data/bin/poweroff initramfs/data/bin/mount initramfs/data/bin/lsblk \
		initramfs/data/bin/cad
	@for n in echo pwd ls cat mkdir mkfifo mksock touch rm rmdir tee chvt fbinfo ttyinfo kbdinfo mouseinfo reboot poweroff mount lsblk cad; do \
		ln -f initramfs/data/bin/cmd initramfs/data/bin/$$n; \
	done
	@cp programs/fb_demo/fb_demo.elf initramfs/data/bin/fb_demo
	@rm -f initramfs/data/bin/shell initramfs/data/bin/hello initramfs/data/bin/fb_test
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
