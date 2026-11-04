/* Run inside the terminal's existing application identity in QEMU. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

static int request(const char *path, const char *text, char *response, size_t capacity) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    strcpy(address.sun_path, path);
    struct timeval timeout = {.tv_sec = 5};
    int result = -1;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) ||
        connect(fd, (struct sockaddr *)&address, sizeof(address)))
        goto done;
    size_t remaining = strlen(text);
    while (remaining) {
        ssize_t count = write(fd, text, remaining);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            goto done;
        text += count;
        remaining -= (size_t)count;
    }
    if (shutdown(fd, SHUT_WR))
        goto done;
    size_t used = 0;
    for (;;) {
        if (used == capacity - 1)
            goto done;
        ssize_t count = read(fd, response + used, capacity - used - 1);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            goto done;
        if (!count)
            break;
        used += (size_t)count;
    }
    response[used] = '\0';
    result = used ? 0 : -1;
done:
    close(fd);
    return result;
}

int main(int argc, char **argv) {
    const char *direct = "/run/leon/notifications.sock", *legacy = "/run/leon/apps.sock";
    char response[131072];
    if (argc == 2 && (!strcmp(argv[1], "--list") || !strcmp(argv[1], "--legacy-list"))) {
        if (request(!strcmp(argv[1], "--list") ? direct : legacy, "{\"op\":\"notifications\"}\n",
                    response, sizeof(response)) ||
            !strstr(response, "\"ok\":true"))
            return 1;
        fputs(response, stdout);
        return 0;
    }
    if (argc != 1 || getuid() != 11005)
        return 2;
    if (request(direct, "{\"op\":\"notifications\"}\n", response, sizeof(response)) ||
        !strstr(response, "\"ok\":false"))
        return 3;
    for (int i = 1; i <= 12; ++i) {
        char message[256], reply[256] = {0};
        snprintf(message, sizeof(message),
                 "{\"op\":\"notify\",\"title\":\"LeonMobile 测试通知 %02d\","
                 "\"app\":\"org.leon.settings\",\"uid\":11000}\n",
                 i);
        if (request(i == 1 ? legacy : direct, message, reply, sizeof(reply)) ||
            !strstr(reply, "\"ok\":true"))
            return 4;
    }
    char result_path[4096];
    const char *home = getenv("HOME");
    if (!home || snprintf(result_path, sizeof(result_path), "%s/notification-test.result", home) >= (int)sizeof(result_path)) return 5;
    FILE *result = fopen(result_path, "w");
    if (!result)
        return 5;
    fputs("12 notifications submitted as terminal UID (11 direct, 1 legacy)\n", result);
    if (fclose(result))
        return 6;
    return 0;
}
