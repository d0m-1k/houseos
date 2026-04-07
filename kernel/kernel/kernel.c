#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <asm/idt.h>
#include <asm/gdt.h>
#include <asm/tss.h>
#include <asm/mm.h>
#include <asm/timer.h>
#include <asm/modes.h>
#include <asm/task.h>
#include <asm/processor.h>
#include <string.h>
#include <drivers/filesystem/memfs.h>
#include <drivers/filesystem/vfs.h>
#include <drivers/filesystem/devfs.h>
#include <drivers/filesystem/fat32.h>
#include <drivers/filesystem/procfs.h>
#include <drivers/vesa.h>
#include <drivers/vga.h>
#include <drivers/filesystem/initramfs.h>
#include <drivers/serial.h>
#include <drivers/tty.h>
#include <drivers/inputdev.h>
#include <drivers/disk.h>
#include <drivers/bootloader.h>
#include <drivers/pci.h>
#include <drivers/pty.h>
#include <drivers/elf_loader.h>
#include <drivers/syscall.h>
#include <devctl.h>

#ifndef CONFIG_PS2_KEYBOARD
#define CONFIG_PS2_KEYBOARD 1
#endif
#ifndef CONFIG_PS2_MOUSE
#define CONFIG_PS2_MOUSE 1
#endif
#ifndef CONFIG_GFX_BACKEND_VESA
#define CONFIG_GFX_BACKEND_VESA 0
#endif
#ifndef CONFIG_DEBUG_KERNEL_SERIAL_LOG
#define CONFIG_DEBUG_KERNEL_SERIAL_LOG 1
#endif
#ifndef CONFIG_KERNEL_FS_DEVFS
#define CONFIG_KERNEL_FS_DEVFS 1
#endif
#ifndef CONFIG_KERNEL_FS_PROCFS
#define CONFIG_KERNEL_FS_PROCFS 1
#endif
#ifndef CONFIG_KERNEL_FS_FAT32
#define CONFIG_KERNEL_FS_FAT32 1
#endif

#if CONFIG_DEBUG_KERNEL_SERIAL_LOG
#define KSERIAL(msg) serial_write(SERIAL_COM1, msg)
#else
#define KSERIAL(msg) do { } while (0)
#endif
#define BOOT_FLAG_DYNAMIC_PARAMS 0x2u

static memfs *fs = NULL;
static devfs_t g_devfs;
static vfs_t g_vfs;
static fat32_fs_t g_rootfat;
static fat32_fs_t g_datafat;
extern vfs_t *g_root_fs_for_syscalls;

typedef struct {
    uint8_t *base;
    uint32_t size;
} fb_ctx_t;

typedef struct {
    const char *tty_path;
    const char *prog_path;
} user_boot_task_t;

static fb_ctx_t g_fb_ctx;
static fb_ctx_t g_vga_ctx;
static user_boot_task_t g_init_boot = { "/dev/tty/S0", "/bin/init" };

static int prepare_empty_user_stack(uint32_t *user_esp_out) {
    uint32_t sp;
    if (!user_esp_out) return -1;
    sp = USER_STACK_TOP & ~3u;
    sp -= 4;
    *(uint32_t*)(uintptr_t)sp = 0;
    sp -= 4;
    *(uint32_t*)(uintptr_t)sp = 0;
    *user_esp_out = sp;
    return 0;
}

static void panic_halt(void) {
    cli();
    while (1) hlt();
}

static ssize_t fb_read(void *ctx, void *buf, size_t size) {
    fb_ctx_t *fb = (fb_ctx_t*)ctx;
    size_t to_copy;
    if (!fb || !buf) return -1;
    to_copy = (size < fb->size) ? size : fb->size;
    memcpy(buf, fb->base, to_copy);
    return (ssize_t)to_copy;
}

static ssize_t fb_write(void *ctx, const void *buf, size_t size) {
    fb_ctx_t *fb = (fb_ctx_t*)ctx;
    size_t to_copy;
    if (!fb || !buf) return -1;
    to_copy = (size < fb->size) ? size : fb->size;
    memcpy(fb->base, buf, to_copy);
    return (ssize_t)to_copy;
}

static int fb_ioctl(void *ctx, uint32_t request, void *arg) {
    fb_ctx_t *fb = (fb_ctx_t*)ctx;
    if (!fb) return -1;
    if (request == DEV_IOCTL_VESA_GET_INFO) {
        dev_fb_info_t *out = (dev_fb_info_t*)arg;
        if (!out) return -1;
        out->width = vesa_get_width();
        out->height = vesa_get_height();
        out->bpp = vesa_get_bpp();
        out->pitch = vesa_get_pitch();
        out->address = vesa_get_framebuffer();
        out->size = fb->size;
        return 0;
    }
    if (request == DEV_IOCTL_VESA_GET_ROTATION) {
        uint32_t *out = (uint32_t*)arg;
        if (!out) return -1;
        *out = vesa_get_rotation();
        return 0;
    }
    if (request == DEV_IOCTL_VESA_SET_ROTATION) {
        uint32_t *in = (uint32_t*)arg;
        if (!in) return -1;
        return vesa_set_rotation(*in) ? 0 : -1;
    }
    return -1;
}

static int vga_ioctl(void *ctx, uint32_t request, void *arg) {
    fb_ctx_t *vga = (fb_ctx_t*)ctx;
    if (!vga) return -1;
    if (request == DEV_IOCTL_VGA_GET_INFO) {
        const volatile void *bda_video_mode_ptr = (const volatile void*)(uintptr_t)0x00000449u;
        uint8_t bda_mode = 0x03;
        dev_vga_info_t *out = (dev_vga_info_t*)arg;
        if (!out) return -1;
        memcpy(&bda_mode, (const void*)bda_video_mode_ptr, sizeof(bda_mode));
        out->cols = VGA_WIDTH;
        out->rows = VGA_HEIGHT;
        out->mode = (uint32_t)bda_mode;
        out->address = (uint32_t)(uintptr_t)vga->base;
        out->size = vga->size;
        return 0;
    }
    return -1;
}

static void user_boot_task(void *arg) {
    user_boot_task_t *cfg = (user_boot_task_t*)arg;
    const char *tty_path;
    const char *prog_path;
    uint32_t entry = 0;
    uint32_t slot_idx = 0;
    uint32_t slot_phys = 0;
    uint32_t user_cr3 = 0;
    uint32_t user_esp = USER_STACK_TOP;

    if (!cfg || !cfg->tty_path || !cfg->prog_path) task_exit();
    tty_path = cfg->tty_path;
    prog_path = cfg->prog_path;
    syscall_bind_stdio(tty_path);
    if (current_task) {
        strncpy(current_task->tty_path, tty_path, sizeof(current_task->tty_path) - 1);
        current_task->tty_path[sizeof(current_task->tty_path) - 1] = '\0';
        strncpy(current_task->prog_path, prog_path, sizeof(current_task->prog_path) - 1);
        current_task->prog_path[sizeof(current_task->prog_path) - 1] = '\0';
        current_task->cmdline[0] = '\0';
    }

    if (!current_task) {
        tty_klog("boot_task: no current_task\n");
        task_exit();
    }
    if (current_task->user_slot == (uint32_t)-1) {
        if (mm_user_slot_alloc(&slot_idx, &slot_phys) != 0) {
            tty_klog("boot_task: no user slot\n");
            task_exit();
        }
        user_cr3 = mm_user_cr3_create(slot_phys);
        if (!user_cr3) {
            mm_user_slot_free(slot_idx);
            tty_klog("boot_task: no user cr3\n");
            task_exit();
        }
        current_task->user_slot = slot_idx;
        current_task->user_phys_base = slot_phys;
        current_task->cr3 = user_cr3;
    }
    mm_switch_cr3(current_task->cr3);

    if (elf_load_from_vfs(&g_vfs, prog_path, &entry) != 0) {
        int ee = elf_get_last_error();
        if (ee == 1) tty_klog("boot_task: elf_load failed (arg)\n");
        else if (ee == 2) tty_klog("boot_task: elf_load failed (info)\n");
        else if (ee == 3) tty_klog("boot_task: elf_load failed (type/size)\n");
        else if (ee == 4) tty_klog("boot_task: elf_load failed (kmalloc)\n");
        else if (ee == 5) tty_klog("boot_task: elf_load failed (read)\n");
        else if (ee == 6) tty_klog("boot_task: elf_load failed (hdr)\n");
        else if (ee == 7) tty_klog("boot_task: elf_load failed (ph)\n");
        else if (ee == 8) tty_klog("boot_task: elf_load failed (memsz)\n");
        else if (ee == 9) tty_klog("boot_task: elf_load failed (segment)\n");
        else if (ee == 10) tty_klog("boot_task: elf_load failed (range)\n");
        else if (ee == 11) tty_klog("boot_task: elf_load failed (entry)\n");
        else tty_klog("boot_task: elf_load failed\n");
        task_exit();
    }
    if (prepare_empty_user_stack(&user_esp) != 0) {
        tty_klog("boot_task: bad user stack\n");
        task_exit();
    }
    sti();
    jump_to_user_image_compat(entry, user_esp);
    task_exit();
}

void kmain(void) {
    int vesa_ok;
    int use_fat_root = 0;
    int root_fat_ready = 0;
    uint32_t root_sel = 0;
    const char *root_disk = NULL;
    serial_init(SERIAL_COM1);
    KSERIAL("kmain: enter\n");

    KSERIAL("kmain: gdt_init\n");
    gdt_init();
    KSERIAL("kmain: tss_init\n");
    tss_init(0x00070000u);

    KSERIAL("kmain: video init\n");
#if CONFIG_GFX_BACKEND_VESA
    vesa_ok = vesa_init() ? 1 : 0;
    if (!vesa_ok) tty_klog("warn: vesa init failed, using vga\n");
#else
    vesa_ok = 0;
#endif

    KSERIAL("kmain: idt_init\n");
    idt_init();
    KSERIAL("kmain: input init\n");
#if CONFIG_PS2_MOUSE
    mouse_init();
#endif
#if CONFIG_PS2_KEYBOARD
    keyboard_init();
#endif
    KSERIAL("kmain: mm_init\n");
    mm_init();
    KSERIAL("kmain: paging_init\n");
    paging_init();
    KSERIAL("kmain: kmalloc_init\n");
    kmalloc_init();
    KSERIAL("kmain: timer_init\n");
    timer_init();

    KSERIAL("kmain: memfs_create\n");
    fs = memfs_create(10 * 1024 * 1024);
    if (!fs) {
        tty_klog("kmain: memfs_create failed, halt\n");
        panic_halt();
    }
    if (vfs_init(&g_vfs) != 0) {
        tty_klog("kmain: vfs_init failed, halt\n");
        panic_halt();
    }
    if (vfs_register_fs(&g_vfs, "memfs", &g_memfs_vfs_ops) != 0) {
        tty_klog("kmain: vfs_register memfs failed, halt\n");
        panic_halt();
    }
#if CONFIG_KERNEL_FS_FAT32
    if (vfs_register_fs(&g_vfs, "fat32", &g_fat32_vfs_ops) != 0) {
        tty_klog("kmain: vfs_register fat32 failed, halt\n");
        panic_halt();
    }
#endif
#if CONFIG_KERNEL_FS_PROCFS
    if (vfs_register_fs(&g_vfs, "procfs", &g_procfs_vfs_ops) != 0) {
        tty_klog("kmain: vfs_register procfs failed, halt\n");
        panic_halt();
    }
#endif
#if CONFIG_KERNEL_FS_DEVFS
    if (devfs_init(&g_devfs, 512 * 1024) == 0) {
        if (vfs_register_fs(&g_vfs, "devfs", &g_devfs_vfs_ops) != 0) {
            tty_klog("kmain: vfs_register devfs failed\n");
        }
    } else {
        tty_klog("kmain: devfs_init failed\n");
    }
#endif
    if (vesa_ok && g_devfs.fs) {
        uint32_t fb_size = vesa_get_pitch() * vesa_get_height();
        g_fb_ctx.base = (uint8_t*)(uintptr_t)vesa_get_framebuffer();
        g_fb_ctx.size = fb_size;
        devfs_create_device_ops(
            &g_devfs, "/vesa",
            MEMFS_DEV_READ | MEMFS_DEV_WRITE,
            fb_read, fb_write, fb_ioctl, &g_fb_ctx
        );
    } else if (g_devfs.fs) {
        g_vga_ctx.base = (uint8_t*)(uintptr_t)VGA_MEMORY_ADDRESS;
        g_vga_ctx.size = VGA_WIDTH * VGA_HEIGHT * 2u;
        devfs_create_device_ops(
            &g_devfs, "/vga",
            MEMFS_DEV_READ | MEMFS_DEV_WRITE,
            fb_read, fb_write, vga_ioctl, &g_vga_ctx
        );
    }

    initramfs_init(fs);
    memfs_create_dir(fs, "/dev");
    memfs_create_dir(fs, "/data");
    memfs_create_dir(fs, "/proc");

    tty_init(fs, &g_devfs);
    KSERIAL("kmain: tty_init done\n");
    inputdev_init(&g_devfs);
    KSERIAL("kmain: inputdev_init done\n");
    KSERIAL("kmain: pty_init skipped\n");
    pci_init();
    KSERIAL("kmain: pci_init done\n");
    if (pci_devfs_init(&g_devfs) != 0) tty_klog("kmain: pci devfs init failed\n");
    KSERIAL("kmain: pci_devfs_init done\n");
    tty_klog("kmain: disk init...\n");
    disk_init(&g_devfs);
    tty_klog("kmain: storage init done\n");
    if (bootloader_dev_init(&g_devfs) != 0) tty_klog("kmain: bootloader dev init failed\n");

    memset(&g_rootfat, 0, sizeof(g_rootfat));
    memset(&g_datafat, 0, sizeof(g_datafat));

#if CONFIG_KERNEL_FS_FAT32
    if (bootloader_get_flags() & BOOT_FLAG_DYNAMIC_PARAMS) root_sel = bootloader_get_root_disk();
    else root_sel = 0;
    if (root_sel == 1u) root_disk = "disk0";

    if (root_disk) {
        if (fat32_init_named(&g_rootfat, root_disk, 2) == 0) {
            root_fat_ready = 1;
        } else {
            tty_klog("warn: selected root disk unavailable, using initramfs\n");
        }
    } else {
        if (fat32_init_named(&g_rootfat, "disk0", 2) == 0) {
            root_fat_ready = 1;
        } else {
            tty_klog("warn: root fat32 unavailable, using initramfs\n");
        }
    }

    if (root_fat_ready && vfs_set_root(&g_vfs, "fat32", &g_rootfat) == 0) {
        char root_src[32];
        const char *rd = root_disk;
        if (!rd) rd = "disk0";
        root_src[0] = '/'; root_src[1] = 'd'; root_src[2] = 'e'; root_src[3] = 'v';
        root_src[4] = '/'; root_src[5] = 'd'; root_src[6] = 'i'; root_src[7] = 's';
        root_src[8] = 'k'; root_src[9] = '/'; root_src[10] = '\0';
        strncat(root_src, rd, sizeof(root_src) - strlen(root_src) - 1);
        strncat(root_src, "p2", sizeof(root_src) - strlen(root_src) - 1);
        (void)vfs_set_root_source(&g_vfs, root_src);
        use_fat_root = 1;
    } else if (root_fat_ready) {
        tty_klog("kmain: vfs_set_root fat32 failed\n");
    }

#endif

    if (!use_fat_root && vfs_set_root(&g_vfs, "memfs", fs) != 0) {
        tty_klog("kmain: vfs_set_root memfs failed, halt\n");
        panic_halt();
    }

#if CONFIG_KERNEL_FS_PROCFS
    if (vfs_mkdir(&g_vfs, "/proc") != 0) {
        vfs_info_t proc_info;
        if (vfs_get_info(&g_vfs, "/proc", &proc_info) != 0 || proc_info.type != VFS_NODE_DIR) {
            tty_klog("kmain: ensure /proc failed\n");
        }
    }
    if (vfs_mount(&g_vfs, "/proc", "procfs", &g_vfs) != 0) {
        tty_klog("kmain: mount /proc procfs failed\n");
    } else {
        (void)vfs_set_mount_source(&g_vfs, "/proc", "procfs");
    }
#endif
#if CONFIG_KERNEL_FS_DEVFS
    if (vfs_mkdir(&g_vfs, "/dev") != 0) {
        vfs_info_t dev_info;
        if (vfs_get_info(&g_vfs, "/dev", &dev_info) != 0 || dev_info.type != VFS_NODE_DIR) {
            tty_klog("kmain: ensure /dev failed\n");
        }
    }
#endif

#if CONFIG_KERNEL_FS_FAT32
    if (use_fat_root) {
        const char *data_disk = root_disk ? root_disk : "disk0";
        if (fat32_init_named(&g_datafat, data_disk, 3) == 0) {
            if (vfs_mount(&g_vfs, "/data", "fat32", &g_datafat) != 0) {
                tty_klog("kmain: mount /data fat32 failed\n");
            } else {
                char data_src[32];
                data_src[0] = '/'; data_src[1] = 'd'; data_src[2] = 'e'; data_src[3] = 'v';
                data_src[4] = '/'; data_src[5] = 'd'; data_src[6] = 'i'; data_src[7] = 's';
                data_src[8] = 'k'; data_src[9] = '/'; data_src[10] = '\0';
                strncat(data_src, data_disk, sizeof(data_src) - strlen(data_src) - 1);
                strncat(data_src, "p3", sizeof(data_src) - strlen(data_src) - 1);
                (void)vfs_set_mount_source(&g_vfs, "/data", data_src);
            }
        }
    }
#endif

    g_root_fs_for_syscalls = &g_vfs;
    syscall_set_devfs_ctx(&g_devfs);
    task_init(NULL);
    {
        int pid = task_create(user_boot_task, &g_init_boot);
        if (pid < 0) tty_klog("kmain: task_create init failed\n");
    }
    task_exit();
}
