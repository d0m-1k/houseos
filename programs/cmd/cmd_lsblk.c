#include "commands.h"
#include "cmd_common.h"
#include <syscall.h>
#include <devctl.h>
#include <stdio.h>
#include <string.h>

int cmd_lsblk(int argc, char **argv, int arg0, const char *cwd) {
    char listbuf[CMD_BUF_SZ];
    int32_t n;
    (void)argc; (void)argv; (void)arg0; (void)cwd;

    n = list("/dev/disk", listbuf, sizeof(listbuf));
    if (n < 0) return 1;
    fprintf(stdout, "NAME SECTORS SIZE TYPE MOUNTPOINT\n");

    {
        uint32_t i = 0;
        while (listbuf[i]) {
            char name[64];
            uint32_t k = 0;
            while (listbuf[i] && listbuf[i] != '\n' && k + 1 < sizeof(name)) name[k++] = listbuf[i++];
            while (listbuf[i] && listbuf[i] != '\n') i++;
            if (listbuf[i] == '\n') i++;
            name[k] = '\0';

            if (k > 0 && name[k - 1] == '*') name[k - 1] = '\0';

            {
                char devpath[256];
                dev_disk_info_t di;
                int fd;
                devpath[0] = '\0';
                if (normalize_path("/dev/disk", name, devpath, sizeof(devpath)) != 0) continue;
                fd = open(devpath, 0);
                if (fd < 0) continue;
                if (ioctl(fd, DEV_IOCTL_DISK_GET_INFO, &di) == 0) {
                    const char *dtype = strchr(name, 'p') ? "partion" : "disk";
                    fprintf(stdout, "%s %u %u %s -\n",
                        name, di.total_sectors, di.total_sectors * di.sector_size, dtype);
                }
                close(fd);
            }
        }
    }
    return 0;
}
