#include "commands.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <syscall.h>

int cmd_udp(int argc, char **argv, int arg0, const char *cwd) {
    int rx = -1;
    int tx = -1;
    struct sockaddr_in dst;
    struct sockaddr_in src;
    uint32_t src_len = sizeof(src);
    char buf[64];
    const char *msg = "udp-loopback-ok";
    int32_t n;
    (void)cwd;

    if (arg0 + 1 >= argc || strcmp(argv[arg0 + 1], "test") != 0) {
        fprintf(stderr, "usage: udp test\n");
        return 1;
    }

    rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (rx < 0) goto fail;
    tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (tx < 0) goto fail;

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(31337);
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(rx, &dst, sizeof(dst)) != 0) goto fail;

    if (sendto(tx, msg, (uint32_t)strlen(msg), 0, &dst, sizeof(dst)) < 0) goto fail;

    memset(buf, 0, sizeof(buf));
    n = recvfrom(rx, buf, sizeof(buf) - 1, 0, &src, &src_len);
    if (n < 0) goto fail;
    buf[(n < (int32_t)sizeof(buf) - 1) ? n : (int32_t)sizeof(buf) - 1] = '\0';
    fprintf(stdout, "udp: recv '%s' from %u\n", buf, (uint32_t)ntohs(src.sin_port));
    close(rx);
    close(tx);
    return 0;

fail:
    if (rx >= 0) close(rx);
    if (tx >= 0) close(tx);
    fprintf(stderr, "udp: test failed\n");
    return 1;
}
