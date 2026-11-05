#define _GNU_SOURCE
#include "wire.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
/* Installed fixture ELF. Root mode is a harmless, bounded diagnostic helper. */
int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    if (!geteuid()) {
        int extra = 0;
        for (int fd = 3; fd < 64; ++fd)
            extra += fcntl(fd, F_GETFD) >= 0;
        char byte;
        printf("helper_uid=%u app=%s caller_uid=%s extra_fds=%d stdin_eof=%d injected_env=%d\n",
               getuid(), getenv("LEON_CALLER_APP"), getenv("LEON_CALLER_UID"), extra,
               read(0, &byte, 1) == 0, getenv("LD_PRELOAD") != NULL);
        if (argc > 1 && !strcmp(argv[1], "wait")) {
            printf("helper_pid=%d\n", getpid());
            sleep(60);
        } else if (argc > 1 && !strcmp(argv[1], "descendant")) {
            if (!fork()) {
                setsid();
                printf("descendant_pid=%d\n", getpid());
                sleep(60);
                _exit(0);
            }
            sleep(60);
        } else if (argc > 1 && !strcmp(argv[1], "flood")) {
            char bytes[1024];
            memset(bytes, 'x', sizeof(bytes));
            for (int i = 0; i < 100; ++i)
                if (write(1, bytes, sizeof(bytes)) < 0)
                    break;
        } else if (argc > 1 && !strcmp(argv[1], "binary")) {
            const unsigned char bytes[] = {0, 255, 10, 27, 0};
            write(1, bytes, sizeof(bytes));
        }
        return 7;
    }
    if (argc != 3)
        return 2;
    char stored[LEON_ROOT_MESSAGE];
    const char *request = argv[2];
    if (!strcmp(request, "@home")) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/root-request.json", getenv("HOME"));
        int input = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        ssize_t size = input < 0 ? -1 : read(input, stored, sizeof(stored) - 1);
        if (input >= 0)
            close(input);
        if (size <= 0 || size >= (ssize_t)sizeof(stored) - 1)
            return 2;
        stored[size] = 0;
        request = stored;
    }
    int fd = root_connect(argv[1]);
    if (fd < 0 || root_send(fd, request, -1) || root_send(fd, "\n", -1))
        return 3;
    struct timeval timeout = {.tv_sec = 125};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    char response[LEON_ROOT_MESSAGE];
    while (!root_receive(fd, response, sizeof(response), NULL))
        fputs(response, stdout);
    close(fd);
    printf("{\"client_uid\":%u}\n", getuid());
    return 0;
}
