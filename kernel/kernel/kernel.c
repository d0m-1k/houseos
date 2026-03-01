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
#include <drivers/vesa.h>
#include <drivers/filesystem/initramfs.h>
#include <drivers/serial.h>
#include <drivers/tty.h>
#include <drivers/inputdev.h>
#include <drivers/elf_loader.h>
#include <drivers/syscall.h>
#include <devctl.h>

static memfs *fs = NULL;
static memfs *devfs = NULL;
extern memfs *g_root_fs_for_syscalls;

extern uint32_t kernel_stack_top;

typedef struct {
    uint8_t *base;
    uint32_t size;
} fb_ctx_t;

typedef struct {
    const char *tty_path;
    const char *prog_path;
} user_boot_task_t;

static fb_ctx_t g_fb_ctx;
static user_boot_task_t g_init_boot = { "/devices/tty0", "/bin/init" };

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
    if (request == DEV_IOCTL_FB_GET_INFO) {
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
    return -1;
}

static void user_boot_task(void *arg) {
    user_boot_task_t *cfg = (user_boot_task_t*)arg;
    const char *tty_path;
    const char *prog_path;
    uint32_t entry = 0;
    uint8_t *user_stack = NULL;

    cli();
    if (!cfg || !cfg->tty_path || !cfg->prog_path) task_exit();
    tty_path = cfg->tty_path;
    prog_path = cfg->prog_path;
    syscall_bind_stdio(tty_path);

    if (elf_load_from_memfs(fs, prog_path, &entry) != 0) {
        tty_klog("boot_task: elf_load failed\n");
        task_exit();
    }

    user_stack = (uint8_t*)kmalloc(4096);
    if (!user_stack) {
        tty_klog("boot_task: no user stack\n");
        task_exit();
    }

    jump_to_ring3(entry, (uint32_t)user_stack + 4096, 0x202);
    task_exit();
}

void kmain(void) {
    int vesa_ok;
    serial_init(SERIAL_COM1);

    gdt_init();
    tss_init(kernel_stack_top);

    vesa_ok = vesa_init() ? 1 : 0;
    if (!vesa_ok) tty_klog("kmain: vesa_init failed, fallback to serial-only\n");

    idt_init();
    mouse_init();
    keyboard_init();
    mm_init();
    kmalloc_init();
    timer_init();
    sti();

    fs = memfs_create(10 * 1024 * 1024);
    if (!fs) {
        tty_klog("kmain: memfs_create failed, halt\n");
        panic_halt();
    }
    initramfs_init(fs);

    memfs_create_dir(fs, "/devices");
    devfs = memfs_create(512 * 1024);
    if (vesa_ok && devfs && memfs_mount(fs, "/devices", devfs) == 0) {
        uint32_t fb_size = vesa_get_pitch() * vesa_get_height();
        g_fb_ctx.base = (uint8_t*)(uintptr_t)vesa_get_framebuffer();
        g_fb_ctx.size = fb_size;
        memfs_create_dir(fs, "/devices/framebuffer");
        memfs_create_device_ops(
            fs, "/devices/framebuffer/buffer",
            MEMFS_DEV_READ | MEMFS_DEV_WRITE,
            fb_read, fb_write, fb_ioctl, &g_fb_ctx
        );
    }

    tty_init(fs);
    inputdev_init(fs);

    g_root_fs_for_syscalls = fs;
    task_init(NULL);
    if (task_create(user_boot_task, &g_init_boot) < 0) tty_klog("kmain: task_create init failed\n");
    task_exit();
}
