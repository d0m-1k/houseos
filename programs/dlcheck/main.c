#include <stdio.h>
#include <string.h>
#include <dylib.h>
#include <hstd_api.h>

int main(int argc, char **argv) {
    const char *lib_path = "/lib/libhstd.so";
    int h;
    int (*get_api)(unsigned int, hstd_api_t*, unsigned int);
    hstd_api_t api;
    char num[16];

    if (argc > 1 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        printf("usage: dlcheck [libpath]\n");
        printf("default libpath: /lib/libhstd.so\n");
        return 0;
    }
    if (argc > 1 && argv[1] && argv[1][0]) lib_path = argv[1];

    h = dl_open(lib_path);
    if (h < 0) {
        fprintf(stderr, "dlcheck: dl_open(%s) failed: %s\n", lib_path, dl_error());
        return 1;
    }

    get_api = (int (*)(unsigned int, hstd_api_t*, unsigned int))dl_sym(h, "hstd_get_api");
    if (!get_api) {
        fprintf(stderr, "dlcheck: dl_sym(hstd_get_api) failed: %s\n", dl_error());
        dl_close(h);
        return 1;
    }

    memset(&api, 0, sizeof(api));
    if (get_api(HSTD_API_VERSION, &api, (unsigned int)sizeof(api)) != 0) {
        fprintf(stderr, "dlcheck: hstd_get_api failed\n");
        dl_close(h);
        return 1;
    }

    if (!api.strlen_fn || !api.utoa_fn) {
        fprintf(stderr, "dlcheck: api table is incomplete\n");
        dl_close(h);
        return 1;
    }

    api.utoa_fn((unsigned int)api.strlen_fn("house"), num, 10u);
    printf("dlcheck: loaded %s, strlen('house')=%s\n", lib_path, num);
    dl_close(h);
    return 0;
}
