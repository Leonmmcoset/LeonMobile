#include "server.h"
void leon_activity(struct leon_server *server) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long long ms = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    if (ms - server->last_activity < 1000)
        return;
    server->last_activity = ms;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    strcpy(address.sun_path, "/run/leon/power.sock");
    if (!connect(fd, (struct sockaddr *)&address, sizeof(address))) {
        const char text[] = "{\"op\":\"activity\"}\n";
        (void)send(fd, text, sizeof(text) - 1, MSG_DONTWAIT | MSG_NOSIGNAL);
    }
    close(fd);
}
