BUILD_DIR := build
INITRAMFS_STAGE := $(BUILD_DIR)/initramfs
ROOTFS_STAGE := $(BUILD_DIR)/rootfs

PROGRAM_DIRS := $(sort $(patsubst %/,%,$(dir $(wildcard */Makefile))))
APP_DIRS := $(filter-out stdlib,$(PROGRAM_DIRS))
APP_DIRS := $(filter-out fb_demo gfxd guishell settings taskmgr terminal composd wmgrd,$(APP_DIRS))

.PHONY: all clean stdlib apps stage

all: stdlib apps stage

stdlib:
	@$(MAKE) -C stdlib all

apps: stdlib
	@for d in $(APP_DIRS); do $(MAKE) -C $$d all; done

stage: apps
	@rm -rf $(INITRAMFS_STAGE) $(ROOTFS_STAGE)
	@mkdir -p $(INITRAMFS_STAGE) $(ROOTFS_STAGE)
	@for d in $(APP_DIRS); do \
		if [ -d $$d/build/initramfs ]; then cp -a $$d/build/initramfs/. $(INITRAMFS_STAGE)/; fi; \
		if [ -d $$d/build/rootfs ]; then cp -a $$d/build/rootfs/. $(ROOTFS_STAGE)/; fi; \
	done

clean:
	@for d in $(PROGRAM_DIRS); do $(MAKE) -C $$d clean; done
	@rm -rf $(BUILD_DIR)
