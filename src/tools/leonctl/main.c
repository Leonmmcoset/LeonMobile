#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fputs("usage: leonctl status|start|stop|restart|reload [SERVICE] | poweroff | reboot\n", stderr);
        return 2;
    }
    const char *path = getenv("LEON_SERVICE_SOCKET");
    if (!path)
        path = "/run/leon/services.sock";
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof(a.sun_path))
        return 2;
    strcpy(a.sun_path, path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a))) {
        perror(path);
        return 1;
    }
    dprintf(fd, "%s %s\n", argv[1], argc > 2 ? argv[2] : "");
    shutdown(fd, SHUT_WR);
    char buf[4096];
    ssize_t n;
    int error = 0;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (n >= 3 && !memcmp(buf, "ERR", 3))
            error = 1;
        fwrite(buf, 1, n, stdout);
    }
    close(fd);
    return error;
}
