BUILD_DIR = build
ASM = nasm

SYSTEM_IMG = $(BUILD_DIR)/system.img
IMG_SECTORS = 131072
STAGE2_SECTORS = 8
CFG_SECTOR = 1
STAGE2_START = 2
PAYLOAD_START = $(shell echo $$(( $(STAGE2_START) + $(STAGE2_SECTORS) )))

BOOT_REGION_START = 1
BOOT_REGION_SECTORS = 32768
ROOTFS_START = 32769
ROOTFS_SECTORS = 245760
DATAFS_START = 278529
DATAFS_SECTORS = 245759
BOOT_FLAGS ?= 1
APPLET_PROFILE ?= core
INITRAMFS_LOAD_ADDR ?= 0x00100000
APPLETS_core = echo printf pwd ls cat grep mkdir touch rm rmdir cp mv ln tee reboot poweroff mount umount
APPLETS_full = echo printf hexdump pwd ls cat grep less mkdir mkfifo mksock touch rm rmdir cp mv ln tee chvt ttyinfo kbdinfo mouseinfo reboot poweroff mount umount lsblk udp bootloader vesa vga
CMD_APPLETS = $(APPLETS_$(APPLET_PROFILE))

ifeq ($(strip $(CMD_APPLETS)),)
$(error Unsupported APPLET_PROFILE='$(APPLET_PROFILE)'. Use core or full)
endif

.PHONY: all clean run debug

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
	kernel_end_hex=$$(nm -n kernel/build/kernel.elf | awk '/ __bss_end$$/ {print $$1; exit}'); \
	if [ -z "$$kernel_end_hex" ]; then \
		echo "ERROR cannot resolve __bss_end from kernel/build/kernel.elf"; \
		exit 1; \
	fi; \
	kernel_end=$$((0x$$kernel_end_hex)); \
	kernel_addr=0x00010000; \
	init_addr=$(INITRAMFS_LOAD_ADDR); \
	init_addr_num=$$((init_addr)); \
	init_end=$$((init_addr + init_sz)); \
	kernel_sec=$$(( (kernel_sz + 511) / 512 )); \
	init_sec=$$(( (init_sz + 511) / 512 )); \
	kernel_lba=$(PAYLOAD_START); \
	init_lba=$$((kernel_lba + kernel_sec)); \
	if [ $$init_addr_num -lt $$kernel_end ]; then \
		echo "ERROR initramfs overlaps kernel image: init_addr=$$(printf '0x%X' $$init_addr_num) kernel_end=$$(printf '0x%X' $$kernel_end)"; \
		exit 1; \
	fi; \
	if [ $$init_addr_num -lt $$((0xA0000)) ] && [ $$init_end -gt $$((0xA0000)) ]; then \
		echo "ERROR initramfs overlaps VGA memory window: init_addr=$$(printf '0x%X' $$init_addr_num) init_end=$$(printf '0x%X' $$init_end)"; \
		exit 1; \
	fi; \
	if [ $$init_end -gt $$((0x400000)) ]; then \
		echo "ERROR initramfs exceeds early identity window: init_end=$$(printf '0x%X' $$init_end) > 0x400000"; \
		exit 1; \
	fi; \
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
		-D CFG_KERNEL_ADDR=$$kernel_addr \
		-D CFG_INITRAMFS_SIZE=$$init_sz \
		-D CFG_INITRAMFS_LBA=$$init_lba \
		-D CFG_INITRAMFS_ADDR=$$init_addr \
		-D CFG_MEMMAP_ADDR=0x00005000 \
		-D CFG_VESA_MODE=0x0118 \
		-D CFG_VESA_INFO_ADDR=0x00009000 \
		-D CFG_VESA_MODE_INFO_ADDR=0x00009100 \
		-D CFG_STAGE2_LBA=$(STAGE2_START) \
		-D CFG_STAGE2_SECTORS=$(STAGE2_SECTORS) \
		-D CFG_FLAGS=$(BOOT_FLAGS) \
		-D CFG_ROOTFS_LBA=$(ROOTFS_START) \
		-D CFG_ROOTFS_SIZE=$$(( $(ROOTFS_SECTORS) * 512 )) \
		-D CFG_ROOT_DISK=0 \
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
	  $$(command -v sfdisk || echo /usr/sbin/sfdisk) --no-reread --no-tell-kernel $@ >/dev/null
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
	@mkdir -p initramfs/data/etc
	@mkdir -p initramfs/data/lib
	@rm -f initramfs/data/bin/*
	@cp programs/init/init.elf initramfs/data/bin/init
	@cp programs/shell/shell.elf initramfs/data/bin/sh
	@cp programs/cmd/cmd.elf initramfs/data/bin/cmd
	@for app in $(CMD_APPLETS); do \
		cp -f initramfs/data/bin/cmd initramfs/data/bin/$$app; \
	done
	@if [ -d programs/build/initramfs/bin ]; then cp -a programs/build/initramfs/bin/. initramfs/data/bin/; fi
	@if [ -d programs/build/initramfs/lib ]; then cp -a programs/build/initramfs/lib/. initramfs/data/lib/; fi
	@strip -s initramfs/data/bin/init initramfs/data/bin/sh initramfs/data/bin/cmd 2>/dev/null || true
	@for app in $(CMD_APPLETS); do \
		strip -s initramfs/data/bin/$$app 2>/dev/null || true; \
	done
	@cp programs/init/init.conf initramfs/data/etc/init.conf
	@cp programs/init/fstab initramfs/data/etc/fstab
	@rm -f initramfs/data/lib/*.a initramfs/data/lib/*.o initramfs/data/lib/*.elf
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
