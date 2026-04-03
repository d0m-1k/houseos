# HouseOS

[English](README.md) | Язык: **Русский**

HouseOS — учебный проект 32-битной ОС для x86 с собственным загрузчиком, ядром, базовым userspace и сборкой загрузочного образа.

## Текущие возможности

- Собственная цепочка загрузки: `MBR -> bootcfg -> stage2 -> kernel`
- Загрузка через BIOS (INT 13h), карта памяти E820, A20, переход в protected mode
- Выбор VESA или VGA при загрузке (через конфиг загрузчика)
- В ядре:
  - планировщик задач
  - слой syscalls
  - VFS + memfs + devfs
  - устройства disk, tty, keyboard, mouse, power, bootloader
- В userspace:
  - `/bin/init`, `/bin/sh`, `/bin/cmd`
  - набор команд через hard link к одному бинарнику (`ls`, `cat`, `mount`, `vesa`, `vga`, `hexdump`, `ln` и др.)

## Структура проекта

```text
.
├── bootloader/     # MBR, stage2, bootcfg (NASM)
├── kernel/         # ядро, arch, драйверы, VFS
├── programs/       # init, shell, cmd, stdlib
├── initramfs/      # сборка содержимого initramfs
└── Makefile        # полная сборка/запуск образа
```

## Сборка

Требования:

- `nasm`
- `gcc` (с поддержкой 32-бит / multilib)
- `ld`, `ar`, `strip`
- `make`
- `qemu-system-i386`
- `cpio`, `sfdisk`, `dd`

Собрать полный образ:

```bash
make -B
```

Результат:

- `build/system.img`

## Запуск

```bash
make run
```

## Прямой UDP Netlog Из Ядра (Realtek)

В HouseOS добавлен минимальный UDP-логгер в ядре (только TX).

- Драйвер: `kernel/drivers/netlog.c`
- Инициализация: после `pci_init()` в `kernel/kernel/kernel.c`
- Подключение логов: `tty_klog()` дублирует строки в UDP в `kernel/drivers/tty.c`

Поддерживаемые NIC:

- `RTL8139` (QEMU сценарий)
- `RTL8168/RTL8169/RTL8161` (реальное железо, TX-only)

Профили по умолчанию:

- `RTL8139`: `10.0.2.15 -> 10.0.2.2:6666` (QEMU usernet)
- `RTL8168/69`: broadcast (`255.255.255.255:6666`) для быстрого лога в локальной сети

Пример запуска QEMU:

```bash
qemu-system-x86_64 -display none -serial stdio \
  -drive format=raw,file=build/system.img \
  -netdev user,id=n0 -device rtl8139,netdev=n0
```

Отладка (GDB stub):

```bash
make debug
# затем в gdb:
target remote localhost:1234
```

## Разметка диска (текущая)

Образ использует фиксированную структуру, чтобы оставить место под будущие ФС и упростить обновления:

- `p1` boot-region (`bootcfg + stage2 + kernel + initramfs`)
- `p2` зарезервирован под rootfs
- `p3` зарезервирован под datafs

`kernel/initramfs` размещаются динамически **внутри p1**, но границы `p2/p3` фиксированы.

## Примечания

- Это учебный/экспериментальный проект, не production-ready.
- Интерфейсы специально простые и могут меняться.

## Лицензия

MIT
