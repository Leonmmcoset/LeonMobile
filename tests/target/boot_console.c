/* Disposable QEMU fixture only. Never installed in the production image. */
#include "boot_log.h"
#include "notify.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>

static const char *endpoint = "/run/leon/boot-console-test.sock";

static int exchange(const char *path, const char *request, char *reply, size_t capacity) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
    struct timeval deadline = {.tv_sec = 3};
    if (fd < 0)
        return -1;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &deadline, sizeof(deadline));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &deadline, sizeof(deadline));
    int failed = connect(fd, (struct sockaddr *)&address, sizeof(address)) ||
                 send(fd, request, strlen(request), MSG_NOSIGNAL) != (ssize_t)strlen(request);
    shutdown(fd, SHUT_WR);
    size_t used = 0;
    while (!failed && used < capacity - 1) {
        ssize_t n = read(fd, reply + used, capacity - used - 1);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            failed = 1;
        if (n <= 0)
            break;
        used += (size_t)n;
    }
    reply[used] = 0;
    close(fd);
    return failed || !used ? -1 : 0;
}

static int mode(void) {
    int fd = open("/dev/tty1", O_RDONLY | O_NOCTTY | O_CLOEXEC);
    int value = -1;
    if (fd >= 0) {
        if (ioctl(fd, KDGETMODE, &value))
            value = -1;
        close(fd);
    }
    return value;
}

static void state(int client) {
    char text[8192] = {0};
    int fd = open("/dev/vcs1", O_RDONLY | O_CLOEXEC);
    ssize_t length = fd < 0 ? -1 : read(fd, text, sizeof(text) - 1);
    if (fd >= 0)
        close(fd);
    if (length > 0) {
        fd = open("/run/leon/boot-console-text.txt", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd >= 0) {
            ssize_t ignored = write(fd, text, (size_t)length);
            (void)ignored;
            close(fd);
        }
    }
    struct vt_stat vt = {0};
    fd = open("/dev/tty0", O_RDONLY | O_NOCTTY | O_CLOEXEC);
    if (fd >= 0) {
        ioctl(fd, VT_GETSTATE, &vt);
        close(fd);
    }
    dprintf(client,
            "{\"ok\":true,\"mode\":%d,\"active_vt\":%d,\"kernel_text\":%s,"
            "\"init_text\":%s,\"manager_text\":%s}\n",
            mode(), vt.v_active,
            strstr(text, "EXT4-fs") || strstr(text, "virtio") || strstr(text, "usb") ? "true" : "false",
            strstr(text, "LeonMobile 1.0.0: init ready") ? "true" : "false",
            strstr(text, "leond: started") ? "true" : "false");
}

static int service(const char *request) {
    char reply[512];
    return exchange("/run/leon/services.sock", request, reply, sizeof(reply)) ||
           strncmp(reply, "OK", 2);
}

static int crash(void) {
    char reply[512];
    if (exchange("/run/leon/services.sock", "status compositor\n", reply, sizeof(reply)))
        return -1;
    char *at = strstr(reply, "pid=");
    pid_t pid = at ? (pid_t)strtol(at + 4, NULL, 10) : -1;
    if (pid <= 1 || kill(pid, SIGKILL))
        return -1;
    for (int i = 0; i < 200; ++i) {
        if (mode() == KD_TEXT)
            return 0;
        usleep(10000);
    }
    return -1;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    if (argc == 2) {
        char request[32], reply[1024];
        if (snprintf(request, sizeof(request), "%s\n", argv[1]) >= (int)sizeof(request) ||
            exchange(endpoint, request, reply, sizeof(reply)))
            return 1;
        fputs(reply, stdout);
        return 0;
    }
    if (argc != 1 || geteuid())
        return 2;
    leon_boot_console_enable();
    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", endpoint);
    unlink(endpoint);
    if (server < 0 || bind(server, (struct sockaddr *)&address, sizeof(address)) ||
        chown(endpoint, 0, 1000) || chmod(endpoint, 0660) || listen(server, 4))
        return 1;
    for (;;) {
        int client = accept4(server, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR)
                continue;
            return 1;
        }
        struct timeval deadline = {.tv_sec = 2};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &deadline, sizeof(deadline));
        struct ucred peer;
        socklen_t size = sizeof(peer);
        char request[32] = {0};
        if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &size) ||
            (peer.uid && peer.uid != 1000) || read(client, request, sizeof(request) - 1) <= 0) {
            close(client);
            continue;
        }
        int failed = 0;
        if (!strcmp(request, "state\n")) {
            state(client);
            close(client);
            continue;
        } else if (!strcmp(request, "release\n"))
            failed = leon_service_ready();
        else if (!strcmp(request, "log\n")) {
            static const char message[] = "<6>LEON_BOOT_KERNEL_DIAGNOSTIC\n";
            int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
            failed = fd < 0 || write(fd, message, sizeof(message) - 1) != (ssize_t)(sizeof(message) - 1);
            if (fd >= 0)
                close(fd);
            leon_boot_log("LEON_BOOT_USERSPACE_DIAGNOSTIC\n");
        } else if (!strcmp(request, "stop\n"))
            failed = service("stop compositor\n");
        else if (!strcmp(request, "start\n"))
            failed = service("start compositor\n");
        else if (!strcmp(request, "crash\n"))
            failed = crash();
        else
            failed = 1;
        dprintf(client, "{\"ok\":%s}\n", failed ? "false" : "true");
        close(client);
    }
}
