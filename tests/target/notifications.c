/* Run inside the terminal's existing application identity in QEMU. */
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

int main(void) {
    for (int i = 1; i <= 12; ++i) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un address = {.sun_family = AF_UNIX};
        strcpy(address.sun_path, "/run/leon/apps.sock");
        struct timeval timeout = {.tv_sec = 5};
        if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
            connect(fd, (struct sockaddr *)&address, sizeof(address)))
            return 1;
        char request[256], response[256] = {0};
        int size = snprintf(request, sizeof(request),
                            "{\"op\":\"notify\",\"title\":\"LeonMobile 测试通知 %02d\"}\n", i);
        if (write(fd, request, (size_t)size) != size)
            return 2;
        shutdown(fd, SHUT_WR);
        ssize_t count = read(fd, response, sizeof(response) - 1);
        close(fd);
        if (count <= 0 || !strstr(response, "\"ok\":true"))
            return 3;
    }
    FILE *result = fopen("/tmp/shade-notify.result", "w");
    if (!result)
        return 4;
    fputs("12 notifications submitted as terminal UID\n", result);
    if (fclose(result))
        return 5;
    return chmod("/tmp/shade-notify.result", 0644) != 0;
}
