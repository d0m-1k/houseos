CC := gcc
LD := ld
AR := ar
AS := nasm

BUILD_DIR := build

CFLAGS := -std=gnu99 -m32 -ffreestanding -fno-stack-protector -O2 -Wall -Wextra \
	-mno-sse -mno-sse2 -mno-mmx -mno-80387 -I../include
LDFLAGS := -m elf_i386 -T ../stdlib/user.ld -nostdlib
ASFLAGS := -f elf32

STDLIB_A := ../stdlib/libstdlib.a
