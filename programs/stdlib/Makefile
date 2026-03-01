.DEFAULT_GOAL := all
include ../common.mk

TARGET := libstdlib.a
OBJS := $(BUILD_DIR)/crt0.o $(BUILD_DIR)/string.o $(BUILD_DIR)/stdlib.o $(BUILD_DIR)/syscall.o $(BUILD_DIR)/stdio.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) rcs $@ $^

$(BUILD_DIR)/crt0.o: crt0.asm
	@mkdir -p $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean
