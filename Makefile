BUILD_DIR = build
ASM = nasm

KCONFIG_FILE ?= .config
KCONFIG_DEFCONFIG ?= configs/houseos_defconfig
KCONFIG_ROOT ?= Kconfig
KCONFIG_CONF ?= /usr/bin/kconfig-conf
KCONFIG_MCONF ?= /usr/bin/kconfig-mconf
AUTO_CONF ?= include/config/auto.conf
AUTO_HEADER ?= include/generated/autoconf.h

-include $(KCONFIG_FILE)
-include $(AUTO_CONF)

define cfg_bool
$(strip $(if $(filter y,$(CONFIG_$(1))),y,$(if $(shell grep -q '^# CONFIG_$(1) is not set' $(KCONFIG_FILE) 2>/dev/null && echo yes),n,)))
endef

SYSTEM_IMG = $(BUILD_DIR)/system.img
IMG_SECTORS ?= $(CONFIG_IMG_SECTORS)
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
BOOT_FLAGS_EXTRA ?= $(CONFIG_BOOT_FLAGS_EXTRA)
INITRAMFS_LOAD_ADDR ?= $(CONFIG_INITRAMFS_LOAD_ADDR)
CONFIG_PS2_KEYBOARD := $(call cfg_bool,PS2_KEYBOARD)
CONFIG_PS2_MOUSE := $(call cfg_bool,PS2_MOUSE)
CONFIG_GRAPHICS_BACKEND_VGA := $(call cfg_bool,GRAPHICS_BACKEND_VGA)
CONFIG_GRAPHICS_BACKEND_VESA := $(call cfg_bool,GRAPHICS_BACKEND_VESA)
CONFIG_VESA_ROTATION := $(call cfg_bool,VESA_ROTATION)
CONFIG_GSHELL := $(call cfg_bool,GSHELL)
CONFIG_BOOT_DYNAMIC_PARAMS := $(call cfg_bool,BOOT_DYNAMIC_PARAMS)
CONFIG_BOOT_DEBUG := $(call cfg_bool,BOOT_DEBUG)
CONFIG_DEBUG_KERNEL_SERIAL_LOG := $(call cfg_bool,DEBUG_KERNEL_SERIAL_LOG)
CONFIG_KERNEL_FS_DEVFS := $(call cfg_bool,KERNEL_FS_DEVFS)
CONFIG_KERNEL_FS_PROCFS := $(call cfg_bool,KERNEL_FS_PROCFS)
CONFIG_KERNEL_FS_FAT32 := $(call cfg_bool,KERNEL_FS_FAT32)
CONFIG_APPLET_ECHO := $(call cfg_bool,APPLET_ECHO)
CONFIG_APPLET_PRINTF := $(call cfg_bool,APPLET_PRINTF)
CONFIG_APPLET_HEXDUMP := $(call cfg_bool,APPLET_HEXDUMP)
CONFIG_APPLET_PWD := $(call cfg_bool,APPLET_PWD)
CONFIG_APPLET_LS := $(call cfg_bool,APPLET_LS)
CONFIG_APPLET_CAT := $(call cfg_bool,APPLET_CAT)
CONFIG_APPLET_GREP := $(call cfg_bool,APPLET_GREP)
CONFIG_APPLET_LESS := $(call cfg_bool,APPLET_LESS)
CONFIG_APPLET_MKDIR := $(call cfg_bool,APPLET_MKDIR)
CONFIG_APPLET_MKFIFO := $(call cfg_bool,APPLET_MKFIFO)
CONFIG_APPLET_MKSOCK := $(call cfg_bool,APPLET_MKSOCK)
CONFIG_APPLET_TOUCH := $(call cfg_bool,APPLET_TOUCH)
CONFIG_APPLET_RM := $(call cfg_bool,APPLET_RM)
CONFIG_APPLET_RMDIR := $(call cfg_bool,APPLET_RMDIR)
CONFIG_APPLET_CP := $(call cfg_bool,APPLET_CP)
CONFIG_APPLET_MV := $(call cfg_bool,APPLET_MV)
CONFIG_APPLET_LN := $(call cfg_bool,APPLET_LN)
CONFIG_APPLET_TEE := $(call cfg_bool,APPLET_TEE)
CONFIG_APPLET_CHVT := $(call cfg_bool,APPLET_CHVT)
CONFIG_APPLET_TTYINFO := $(call cfg_bool,APPLET_TTYINFO)
CONFIG_APPLET_KBDINFO := $(call cfg_bool,APPLET_KBDINFO)
CONFIG_APPLET_MOUSEINFO := $(call cfg_bool,APPLET_MOUSEINFO)
CONFIG_APPLET_REBOOT := $(call cfg_bool,APPLET_REBOOT)
CONFIG_APPLET_POWEROFF := $(call cfg_bool,APPLET_POWEROFF)
CONFIG_APPLET_MOUNT := $(call cfg_bool,APPLET_MOUNT)
CONFIG_APPLET_UMOUNT := $(call cfg_bool,APPLET_UMOUNT)
CONFIG_APPLET_LSBLK := $(call cfg_bool,APPLET_LSBLK)
CONFIG_APPLET_UDP := $(call cfg_bool,APPLET_UDP)
CONFIG_APPLET_BOOTLOADER := $(call cfg_bool,APPLET_BOOTLOADER)
CONFIG_APPLET_VESA := $(call cfg_bool,APPLET_VESA)
CONFIG_APPLET_VGA := $(call cfg_bool,APPLET_VGA)
QEMU_MEM ?= $(patsubst "%",%,$(CONFIG_QEMU_MEM))
QEMU_SERIAL ?= $(patsubst "%",%,$(CONFIG_QEMU_SERIAL))

CMD_APPLETS = \
	$(if $(filter y,$(CONFIG_APPLET_ECHO)),echo) \
	$(if $(filter y,$(CONFIG_APPLET_PRINTF)),printf) \
	$(if $(filter y,$(CONFIG_APPLET_HEXDUMP)),hexdump) \
	$(if $(filter y,$(CONFIG_APPLET_PWD)),pwd) \
	$(if $(filter y,$(CONFIG_APPLET_LS)),ls) \
	$(if $(filter y,$(CONFIG_APPLET_CAT)),cat) \
	$(if $(filter y,$(CONFIG_APPLET_GREP)),grep) \
	$(if $(filter y,$(CONFIG_APPLET_LESS)),less) \
	$(if $(filter y,$(CONFIG_APPLET_MKDIR)),mkdir) \
	$(if $(filter y,$(CONFIG_APPLET_MKFIFO)),mkfifo) \
	$(if $(filter y,$(CONFIG_APPLET_MKSOCK)),mksock) \
	$(if $(filter y,$(CONFIG_APPLET_TOUCH)),touch) \
	$(if $(filter y,$(CONFIG_APPLET_RM)),rm) \
	$(if $(filter y,$(CONFIG_APPLET_RMDIR)),rmdir) \
	$(if $(filter y,$(CONFIG_APPLET_CP)),cp) \
	$(if $(filter y,$(CONFIG_APPLET_MV)),mv) \
	$(if $(filter y,$(CONFIG_APPLET_LN)),ln) \
	$(if $(filter y,$(CONFIG_APPLET_TEE)),tee) \
	$(if $(filter y,$(CONFIG_APPLET_CHVT)),chvt) \
	$(if $(filter y,$(CONFIG_APPLET_TTYINFO)),ttyinfo) \
	$(if $(filter y,$(CONFIG_APPLET_KBDINFO)),kbdinfo) \
	$(if $(filter y,$(CONFIG_APPLET_MOUSEINFO)),mouseinfo) \
	$(if $(filter y,$(CONFIG_APPLET_REBOOT)),reboot) \
	$(if $(filter y,$(CONFIG_APPLET_POWEROFF)),poweroff) \
	$(if $(filter y,$(CONFIG_APPLET_MOUNT)),mount) \
	$(if $(filter y,$(CONFIG_APPLET_UMOUNT)),umount) \
	$(if $(filter y,$(CONFIG_APPLET_LSBLK)),lsblk) \
	$(if $(filter y,$(CONFIG_APPLET_UDP)),udp) \
	$(if $(filter y,$(CONFIG_APPLET_BOOTLOADER)),bootloader) \
	$(if $(filter y,$(CONFIG_APPLET_VESA)),vesa) \
	$(if $(filter y,$(CONFIG_APPLET_VGA)),vga)

IMG_SECTORS ?= 131072
BOOT_FLAGS_EXTRA ?= 0x0
INITRAMFS_LOAD_ADDR ?= 0x00100000
CONFIG_PS2_KEYBOARD ?= y
CONFIG_PS2_MOUSE ?= y
CONFIG_GSHELL ?= n
CONFIG_KERNEL_FS_DEVFS ?= y
CONFIG_KERNEL_FS_PROCFS ?= y
CONFIG_KERNEL_FS_FAT32 ?= y
QEMU_MEM ?= 4G
QEMU_SERIAL ?= stdio
VGA_MODE ?= $(CONFIG_GRAPHICS_VGA_MODE)
VESA_MODE ?= $(CONFIG_GRAPHICS_VESA_MODE)
VGA_MODE ?= 0x03
VESA_MODE ?= 0x0118

ifeq ($(strip $(BOOT_FLAGS_EXTRA)),)
BOOT_FLAGS_EXTRA := 0x0
endif
ifeq ($(strip $(CONFIG_PS2_KEYBOARD)),)
CONFIG_PS2_KEYBOARD := y
endif
ifeq ($(strip $(CONFIG_PS2_MOUSE)),)
CONFIG_PS2_MOUSE := y
endif
ifeq ($(strip $(CONFIG_GSHELL)),)
CONFIG_GSHELL := n
endif
ifeq ($(strip $(CONFIG_KERNEL_FS_DEVFS)),)
CONFIG_KERNEL_FS_DEVFS := y
endif
ifeq ($(strip $(CONFIG_KERNEL_FS_PROCFS)),)
CONFIG_KERNEL_FS_PROCFS := y
endif
ifeq ($(strip $(CONFIG_KERNEL_FS_FAT32)),)
CONFIG_KERNEL_FS_FAT32 := y
endif
ifeq ($(strip $(CONFIG_GRAPHICS_BACKEND_VGA)),)
CONFIG_GRAPHICS_BACKEND_VGA := y
endif
ifeq ($(strip $(CONFIG_GRAPHICS_BACKEND_VESA)),)
CONFIG_GRAPHICS_BACKEND_VESA := n
endif
ifeq ($(strip $(CONFIG_BOOT_DYNAMIC_PARAMS)),)
CONFIG_BOOT_DYNAMIC_PARAMS := y
endif
ifeq ($(strip $(CONFIG_BOOT_DEBUG)),)
CONFIG_BOOT_DEBUG := n
endif
ifeq ($(strip $(CONFIG_DEBUG_KERNEL_SERIAL_LOG)),)
CONFIG_DEBUG_KERNEL_SERIAL_LOG := y
endif
ifeq ($(strip $(VGA_MODE)),)
VGA_MODE := 0x03
endif
ifeq ($(strip $(VESA_MODE)),)
VESA_MODE := 0x0118
endif

VIDEO_OUTPUT := $(if $(filter y,$(CONFIG_GRAPHICS_BACKEND_VESA)),0,1)
BOOT_DYNAMIC_FLAG := $(if $(filter y,$(CONFIG_BOOT_DYNAMIC_PARAMS)),0x2,0x0)
BOOT_DEBUG_FLAG := $(if $(filter y,$(CONFIG_BOOT_DEBUG)),0x1,0x0)
BOOT_FLAGS ?= $(shell printf '0x%X' $$(( ($(BOOT_FLAGS_EXTRA)) | ($(BOOT_DYNAMIC_FLAG)) | ($(BOOT_DEBUG_FLAG)) )))

ifeq ($(strip $(CMD_APPLETS)),)
$(error No applets selected. Enable at least one APPLET_* option in .config/menuconfig)
endif

.PHONY: all clean run debug config defconfig oldconfig olddefconfig menuconfig savedefconfig printconfig syncconfig auto-files FORCE

all: $(SYSTEM_IMG)

config: oldconfig

defconfig:
	@cp $(KCONFIG_DEFCONFIG) $(KCONFIG_FILE)
	@KCONFIG_CONFIG=$(KCONFIG_FILE) $(KCONFIG_CONF) --olddefconfig $(KCONFIG_ROOT)
	@$(MAKE) --no-print-directory auto-files

oldconfig:
	@KCONFIG_CONFIG=$(KCONFIG_FILE) $(KCONFIG_CONF) --oldconfig $(KCONFIG_ROOT)
	@$(MAKE) --no-print-directory auto-files

olddefconfig:
	@KCONFIG_CONFIG=$(KCONFIG_FILE) $(KCONFIG_CONF) --olddefconfig $(KCONFIG_ROOT)
	@$(MAKE) --no-print-directory auto-files

syncconfig: olddefconfig

menuconfig:
	@if [ ! -t 0 ] || [ ! -t 1 ]; then \
		echo "menuconfig requires an interactive TTY"; \
		exit 1; \
	fi
	@KCONFIG_CONFIG=$(KCONFIG_FILE) $(KCONFIG_MCONF) $(KCONFIG_ROOT)
	@$(MAKE) --no-print-directory auto-files

savedefconfig:
	@KCONFIG_CONFIG=$(KCONFIG_FILE) $(KCONFIG_CONF) --savedefconfig $(KCONFIG_DEFCONFIG) $(KCONFIG_ROOT)

printconfig:
	@cat $(KCONFIG_FILE)

auto-files: $(KCONFIG_FILE)
	@mkdir -p $(dir $(AUTO_CONF)) $(dir $(AUTO_HEADER))
	@cp $(KCONFIG_FILE) $(AUTO_CONF)
	@{ \
		echo "/* Auto-generated from $(KCONFIG_FILE). */"; \
		awk '\
			/^CONFIG_[A-Za-z0-9_]+=y$$/ { \
				key = $$0; sub(/=.*/, "", key); \
				print "#define " key " 1"; \
				next; \
			} \
			/^CONFIG_[A-Za-z0-9_]+=.*/ { \
				key = $$0; sub(/=.*/, "", key); \
				val = $$0; sub(/^[^=]*=/, "", val); \
				print "#define " key " " val; \
			} \
		' $(KCONFIG_FILE); \
	} > $(AUTO_HEADER)

$(SYSTEM_IMG): syncconfig $(BUILD_DIR)/bootloader $(BUILD_DIR)/programs $(BUILD_DIR)/initramfs $(BUILD_DIR)/kernel | $(BUILD_DIR)
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
			-D CFG_VIDEO_OUTPUT=$(VIDEO_OUTPUT) \
			-D CFG_VESA_MODE=$(VESA_MODE) \
			-D CFG_VGA_MODE=$(VGA_MODE) \
			-D CFG_VESA_INFO_ADDR=0x00009000 \
			-D CFG_VESA_MODE_INFO_ADDR=0x00009100 \
			-D CFG_VESA_MODES_ADDR=0x00000000 \
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

$(BUILD_DIR)/programs: FORCE | $(BUILD_DIR)
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
	@$(MAKE) -C kernel \
		CONFIG_XHCI=n CONFIG_USBKBD=n \
		CONFIG_PS2_KEYBOARD=$(CONFIG_PS2_KEYBOARD) CONFIG_PS2_MOUSE=$(CONFIG_PS2_MOUSE) \
		CONFIG_GSHELL=$(CONFIG_GSHELL) \
		CONFIG_KERNEL_FS_DEVFS=$(CONFIG_KERNEL_FS_DEVFS) \
		CONFIG_KERNEL_FS_PROCFS=$(CONFIG_KERNEL_FS_PROCFS) \
		CONFIG_KERNEL_FS_FAT32=$(CONFIG_KERNEL_FS_FAT32) \
		CONFIG_GFX_BACKEND=$(if $(filter y,$(CONFIG_GRAPHICS_BACKEND_VESA)),vesa,vga) \
		CONFIG_VESA_ROTATION=$(CONFIG_VESA_ROTATION) \
		CONFIG_DEBUG_KERNEL_SERIAL_LOG=$(CONFIG_DEBUG_KERNEL_SERIAL_LOG)
	@cp kernel/build/kernel.bin $@.bin

$(BUILD_DIR):
	@mkdir -p $@

run: $(SYSTEM_IMG)
	@echo "QEMU  $@"
	@qemu-system-i386 -drive format=raw,file=$(SYSTEM_IMG) -serial $(QEMU_SERIAL) -m $(QEMU_MEM)

debug: $(SYSTEM_IMG)
	@echo "QEMU  $@"
	@echo "Attach to system: target remote localhost:1234"
	@qemu-system-i386 -drive format=raw,file=$(SYSTEM_IMG) -S -s -serial $(QEMU_SERIAL) -m $(QEMU_MEM)

clean:
	@echo "CLEAN"
	@$(MAKE) -C bootloader clean
	@$(MAKE) -C programs clean
	@$(MAKE) -C kernel clean
	@rm -f $(AUTO_CONF) $(AUTO_HEADER)
	@rm -rf $(BUILD_DIR)
