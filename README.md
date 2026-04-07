# 🖥️ Минимальная 32-битная ОС (x86)

Учебный проект небольшой операционной системы для архитектуры **i386 (x86)** с собственным загрузчиком, переходом в защищённый режим и базовым ядром с драйверами.

> 🚧 **Статус:** учебный/экспериментальный проект. Не предназначен для реального использования.

---

## ✨ Возможности

### 🔹 Загрузчик (bootloader)

* Собственный **MBR + Stage 2**
* Чтение с диска через BIOS (INT 13h)
* Включение **A20**
* Получение карты памяти через **E820**
* Настройка **GDT**
* Переход в **Protected Mode (32-bit)**

### 🔹 Ядро (kernel)

* Базовая архитектура i386
* Обработчик прерываний (**IDT, PIC**)
* Доступ к портам ввода-вывода (`inb/outb`)
* Простая система управления памятью:

  * Разбор карты памяти (E820)
  * Базовый heap-аллокатор (`kmalloc/kfree`)
* Драйверы:

  * **VGA-текстовый режим (80x25)**
  * **TTY (терминал)**
  * **PS/2 клавиатура**
* Простая оболочка и демонстрационные программы (например, `snake`)

---

## 📁 Структура проекта

```
.
├── bootloader/      # Загрузчик (ASM, NASM)
├── kernel/
│   ├── arch/i386/   # Архитектурно-зависимый код
│   ├── drivers/     # VGA, TTY, Keyboard
│   ├── lib/         # stdio, string
│   ├── progs/       # init, shell, snake
│   └── linker.ld    # Скрипт компоновки ядра
└── Makefile         # Сборка образа системы
```

---

## 🛠️ Сборка

### Требования

* `nasm`
* `gcc` (i386 cross-compiler или мультилиб)
* `kconfig-frontends` (`kconfig-conf`, `kconfig-mconf`, `kconfig-config2h`)
* `make`
* `qemu-system-i386`
* `dd`

### Собрать образ системы:

```bash
make
```

Kconfig workflow:

```bash
make defconfig
make oldconfig
make olddefconfig
make menuconfig
make savedefconfig
```

Это создаст:

```
build/system.img
```

---

## 🚀 Запуск в QEMU

```bash
make run
```

Configure enabled applets:

```bash
make menuconfig   # Userland applets section
```

## Unix-like Compatibility Status

- procfs: `/proc/version` returns OS release string.
- procfs: `/proc/self/*` aliases supported.
- procfs: pid/fd directories support trailing slash (`/proc/<pid>/`, `/proc/<pid>/fd/`).
- userland: `cmd` works as multi-call binary (`argv[0]` dispatch).
- userland: `cmd install` resolves executable via `/proc/self/exe` with fallback.
- shell: wait loop exits on terminated or non-existent child.

Quick contract check:
Use `make` and a QEMU serial boot run as compatibility smoke checks.

## Direct Kernel UDP Netlog (Realtek)

HouseOS now has a minimal in-kernel UDP logger (TX path).

- Driver file: `kernel/drivers/netlog.c`
- Auto-init: after `pci_init()` in `kernel/kernel/kernel.c`
- Log hook: every `tty_klog()` line is mirrored to UDP in `kernel/drivers/tty.c`

Supported NICs:

- `RTL8139` (QEMU flow)
- `RTL8168/RTL8169/RTL8161` (real hardware, TX-only)

Default profiles:

- `RTL8139`: `10.0.2.15 -> 10.0.2.2:6666` (QEMU usernet)
- `RTL8168/69`: broadcast (`255.255.255.255:6666`) for quick LAN log pickup

Example QEMU run:

```bash
qemu-system-x86_64 -display none -serial stdio \
  -drive format=raw,file=build/system.img \
  -netdev user,id=n0 -device rtl8139,netdev=n0
```

Debug (GDB stub):

```bash
make debug
```

Затем подключиться:

```
target remote localhost:1234
```

---

## 📌 Как работает загрузка

1. **MBR (0x7C00)** загружает Stage 2 с диска.
2. **Stage 2 (0x7E00)**:

   * Загружает дополнительные сектора
   * Получает карту памяти (E820)
   * Включает A20
   * Загружает GDT
   * Переходит в Protected Mode
3. Управление передаётся ядру по адресу `0x10000`.

---

## 🎯 Цели проекта

* Понять, как работает загрузка ПК на низком уровне
* Изучить переход в Protected Mode
* Разобраться с памятью и прерываниями
* Реализовать минимальный набор драйверов

---

## 📚 Полезные источники

* OSDev Wiki: [https://wiki.osdev.org](https://wiki.osdev.org)
* Intel Manuals (Vol. 3)
* Bran’s Kernel Development Tutorial

---

## 🤝 Вклад в проект

Pull requests приветствуются. Если хочешь добавить:

* ACPI
* Paging
* файловую систему
* многозадачность
  — буду рад обсудить!

---

## 📄 Лицензия

GPL-2.0-only
