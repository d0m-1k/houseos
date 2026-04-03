#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="/home/dom4k/houseos"
IMG="$ROOT_DIR/build/system.img"
LOG_DIR="$ROOT_DIR/build"
LOG_FILE="$LOG_DIR/serial-vnc.log"

mkdir -p "$LOG_DIR"

if [[ ! -f "$IMG" ]]; then
  make -C "$ROOT_DIR"
fi

exec /usr/bin/qemu-system-i386 \
  -machine pc \
  -drive if=none,id=osdisk,format=raw,file="$IMG",cache=writeback,aio=threads \
  -device ide-hd,drive=osdisk,bus=ide.0,unit=0 \
  -m 4G \
  -display none \
  -vnc 127.0.0.1:1 \
  -serial file:"$LOG_FILE"
