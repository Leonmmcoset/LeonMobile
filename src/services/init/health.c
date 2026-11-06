#include "init.h"
#include "state.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
static int exchange(const char *path, const char *request, char *out, size_t capacity) {
    /* PID 1 must stay responsive even if a server's accept queue is full. A
     * nonblocking AF_UNIX connect can return EAGAIN; retry on the next tick. */
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
    int result = -1;
    if (connect(fd, (struct sockaddr *)&address, sizeof(address))) {
        if (errno != EINPROGRESS) goto done;
        struct pollfd p = {fd, POLLOUT, 0};
        int error = 0; socklen_t length = sizeof(error);
        if (poll(&p, 1, 200) <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) || error)
            goto done;
    }
    if (send(fd, request, strlen(request), MSG_NOSIGNAL) != (ssize_t)strlen(request)) goto done;
    shutdown(fd, SHUT_WR);
    size_t used = 0;
    for (int i = 0; i < 40 && used + 1 < capacity; ++i) {
        struct pollfd p = {fd, POLLIN, 0};
        if (poll(&p, 1, 5) <= 0) continue;
        ssize_t n = read(fd, out + used, capacity - 1 - used);
        if (n < 0) { if (errno == EAGAIN || errno == EINTR) continue; goto done; }
        if (!n) { out[used] = 0; result = 0; break; }
        used += (size_t)n;
    }
done:
    close(fd); return result;
}
static int ready(void) {
    char text[16384];
    if (exchange("/run/leon/services.sock", "status\n", text, sizeof(text))) return 0;
    const char *required[] = {"logging", "authentication", "apps", "notifications", "power", "portal", "root",
        "settings", "documents", "intents", "audio", "input-method", "network", "time", "shell", "compositor", NULL};
    for (int i = 0; required[i]; ++i) {
        char marker[96]; snprintf(marker, sizeof(marker), "%s\trunning\t", required[i]);
        char *entry = strstr(text, marker);
        if (!entry || (entry != text && entry[-1] != '\n')) return 0;
        char *end = strchr(entry, '\n'), *health = strstr(entry, "health=ready");
        if (!health || (end && health > end)) return 0;
    }
    return !exchange("/run/leon/wm.sock", "boot-status", text, sizeof(text)) && !strcmp(text, "ready\n");
}
int leon_boot_health_tick(void) {
    static long long start, last, stable;
    static int complete;
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    long long seconds = now.tv_sec;
    if (complete) return 0;
    if (!start) start = seconds;
    if (last == seconds) return 0;
    last = seconds;
    int healthy = ready();
    if (!healthy) stable = 0;
    else if (!stable) stable = seconds;
    if (healthy && seconds - stable >= 3) {
        struct leon_boot_store store;
        struct leon_boot_state state;
        char id[37];
        if (!leon_boot_store_open(&store, "/data/leon/boot")) {
            if (!leon_boot_state_load(&store, &state) && !leon_boot_id(id) &&
                !leon_boot_success(&state, id) && !leon_boot_state_save(&store, &state)) {
                complete = 1;
                leon_boot_publish(&state, "/run/leon-boot");
                leon_boot_log("leon-init: boot healthy (services ready and desktop presented)\n");
            }
            leon_boot_store_close(&store);
        }
    }
    if (!complete && seconds - start >= 180) {
        struct leon_boot_store store; struct leon_boot_state state;
        if (!leon_boot_store_open(&store, "/data/leon/boot")) {
            if (!leon_boot_state_load(&store, &state)) { state.reason = LEON_BOOT_TIMEOUT; leon_boot_state_save(&store, &state); }
            leon_boot_store_close(&store);
        }
        leon_boot_log("leon-init: boot health timeout; rebooting (recovery after three failures)\n");
        return -1;
    }
    return 0;
}
