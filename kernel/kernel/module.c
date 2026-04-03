#include <kernel/module.h>
#include <drivers/tty.h>

int kernel_modules_run(const kernel_module_t *mods, uint32_t count, kernel_boot_ctx_t *ctx) {
    if (!mods) return -1;

    for (uint32_t i = 0; i < count; i++) {
        int rc;
        if (!mods[i].init) continue;
        tty_klog("kmod: init ");
        tty_klog(mods[i].name ? mods[i].name : "(unnamed)");
        tty_klog("\n");
        rc = mods[i].init(ctx);
        if (rc != 0) {
            tty_klog("kmod: fail ");
            tty_klog(mods[i].name ? mods[i].name : "(unnamed)");
            tty_klog("\n");
            if (mods[i].flags & KERNEL_MODULE_REQUIRED) return -1;
        }
    }
    return 0;
}
