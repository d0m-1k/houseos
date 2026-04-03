#include <drivers/pty.h>
#include <asm/processor.h>
#include <devctl.h>
#include <string.h>

#define PTY_PAIR_COUNT 4u
#define PTY_RING_SIZE 4096u

typedef struct {
    uint8_t data[PTY_RING_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} pty_ring_t;

typedef struct {
    uint8_t allocated;
    pty_ring_t master_to_slave;
    pty_ring_t slave_to_master;
} pty_pair_t;

typedef struct {
    pty_pair_t *pair;
    uint8_t is_master;
    uint32_t index;
} pty_dev_ctx_t;

static pty_pair_t g_pairs[PTY_PAIR_COUNT];
static pty_dev_ctx_t g_master_ctx[PTY_PAIR_COUNT];
static pty_dev_ctx_t g_slave_ctx[PTY_PAIR_COUNT];

static void ring_reset(pty_ring_t *r) {
    if (!r) return;
    r->head = 0;
    r->tail = 0;
    r->count = 0;
}

static void pair_reset(pty_pair_t *p) {
    if (!p) return;
    ring_reset(&p->master_to_slave);
    ring_reset(&p->slave_to_master);
}

static uint32_t ring_push(pty_ring_t *r, const uint8_t *buf, uint32_t size) {
    uint32_t i;
    if (!r || !buf) return 0;
    for (i = 0; i < size; i++) {
        if (r->count >= PTY_RING_SIZE) {
            r->head = (r->head + 1u) % PTY_RING_SIZE;
            r->count--;
        }
        r->data[r->tail] = buf[i];
        r->tail = (r->tail + 1u) % PTY_RING_SIZE;
        r->count++;
    }
    return size;
}

static uint32_t ring_pop(pty_ring_t *r, uint8_t *buf, uint32_t size) {
    uint32_t n = 0;
    if (!r || !buf || size == 0u) return 0;
    while (n < size && r->count > 0u) {
        buf[n++] = r->data[r->head];
        r->head = (r->head + 1u) % PTY_RING_SIZE;
        r->count--;
    }
    return n;
}

static ssize_t pty_read(void *ctx, void *buf, size_t size) {
    pty_dev_ctx_t *d = (pty_dev_ctx_t*)ctx;
    pty_ring_t *src;
    uint32_t got;
    if (!d || !buf || size == 0u || !d->pair) return -1;
    if (!d->pair->allocated) return -1;
    src = d->is_master ? &d->pair->slave_to_master : &d->pair->master_to_slave;

    while (1) {
        got = ring_pop(src, (uint8_t*)buf, (uint32_t)size);
        if (got > 0u) return (ssize_t)got;
        if (!d->pair->allocated) return 0;
        sti();
        hlt();
        cli();
    }
}

static ssize_t pty_write(void *ctx, const void *buf, size_t size) {
    pty_dev_ctx_t *d = (pty_dev_ctx_t*)ctx;
    pty_ring_t *dst;
    if (!d || !buf || !d->pair) return -1;
    if (!d->pair->allocated) return -1;
    if (size == 0u) return 0;
    dst = d->is_master ? &d->pair->master_to_slave : &d->pair->slave_to_master;
    return (ssize_t)ring_push(dst, (const uint8_t*)buf, (uint32_t)size);
}

static void build_pair_paths(uint32_t idx, char *master, uint32_t mcap, char *slave, uint32_t scap) {
    if (master && mcap >= 11u) {
        strcpy(master, "/dev/pty/");
        master[9] = (char)('0' + (idx % 10u));
        master[10] = '\0';
    }
    if (slave && scap >= 11u) {
        strcpy(slave, "/dev/pts/");
        slave[9] = (char)('0' + (idx % 10u));
        slave[10] = '\0';
    }
}

static int pty_dev_ioctl(void *ctx, uint32_t request, void *arg) {
    pty_dev_ctx_t *d = (pty_dev_ctx_t*)ctx;
    pty_ring_t *src;
    if (!d || !d->pair) return -1;
    if (request == DEV_IOCTL_PTY_GET_READABLE) {
        if (!arg) return -1;
        if (!d->pair->allocated) {
            *(uint32_t*)arg = 0u;
            return 0;
        }
        src = d->is_master ? &d->pair->slave_to_master : &d->pair->master_to_slave;
        *(uint32_t*)arg = src->count;
        return 0;
    }
    if (request == DEV_IOCTL_PTY_RESET) {
        if (!d->pair->allocated) return -1;
        pair_reset(d->pair);
        return 0;
    }
    return -1;
}

static int ptmx_ioctl(void *ctx, uint32_t request, void *arg) {
    (void)ctx;
    if (request == DEV_IOCTL_PTY_ALLOC) {
        dev_pty_alloc_t *out = (dev_pty_alloc_t*)arg;
        if (!out) return -1;
        for (uint32_t i = 0; i < PTY_PAIR_COUNT; i++) {
            if (g_pairs[i].allocated) continue;
            g_pairs[i].allocated = 1u;
            pair_reset(&g_pairs[i]);
            out->index = i;
            build_pair_paths(i, out->master_path, sizeof(out->master_path), out->slave_path, sizeof(out->slave_path));
            return 0;
        }
        return -1;
    }
    if (request == DEV_IOCTL_PTY_FREE) {
        uint32_t idx;
        if (!arg) return -1;
        idx = *(uint32_t*)arg;
        if (idx >= PTY_PAIR_COUNT) return -1;
        g_pairs[idx].allocated = 0u;
        pair_reset(&g_pairs[idx]);
        return 0;
    }
    return -1;
}

void pty_init(devfs_t *devfs) {
    char path[16];
    if (!devfs) return;
    (void)devfs_create_dir(devfs, "/pty");
    (void)devfs_create_dir(devfs, "/pts");

    for (uint32_t i = 0; i < PTY_PAIR_COUNT; i++) {
        memset(&g_pairs[i], 0, sizeof(g_pairs[i]));
        memset(&g_master_ctx[i], 0, sizeof(g_master_ctx[i]));
        memset(&g_slave_ctx[i], 0, sizeof(g_slave_ctx[i]));
        g_master_ctx[i].pair = &g_pairs[i];
        g_master_ctx[i].is_master = 1u;
        g_master_ctx[i].index = i;
        g_slave_ctx[i].pair = &g_pairs[i];
        g_slave_ctx[i].is_master = 0u;
        g_slave_ctx[i].index = i;

        strcpy(path, "/pty/");
        path[5] = (char)('0' + (i % 10u));
        path[6] = '\0';
        (void)devfs_create_device_ops(devfs, path, MEMFS_DEV_READ | MEMFS_DEV_WRITE,
            pty_read, pty_write, pty_dev_ioctl, &g_master_ctx[i]);

        strcpy(path, "/pts/");
        path[5] = (char)('0' + (i % 10u));
        path[6] = '\0';
        (void)devfs_create_device_ops(devfs, path, MEMFS_DEV_READ | MEMFS_DEV_WRITE,
            pty_read, pty_write, pty_dev_ioctl, &g_slave_ctx[i]);
    }

    (void)devfs_create_device_ops(devfs, "/ptmx", 0, 0, 0, ptmx_ioctl, 0);
}
