.DEFAULT_GOAL := all
include ../common.mk

TARGET := init.elf
OBJS := $(BUILD_DIR)/crt0.o $(BUILD_DIR)/main.o

all: $(TARGET)

$(TARGET): $(OBJS) $(STDLIB_A)
	$(LD) $(LDFLAGS) $(OBJS) $(STDLIB_A) -o $@

$(BUILD_DIR)/crt0.o: ../stdlib/crt0.asm
	@mkdir -p $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/main.o: main.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean
