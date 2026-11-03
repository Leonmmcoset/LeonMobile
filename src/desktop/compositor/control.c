/* SPDX-License-Identifier: CC0-1.0
 * Based on wlroots 0.20 tinywl; mobile policy additions by LeonMobile. */
#include "server.h"
#include <sys/time.h>
/* The shell owns navigation. This socket is mode 0600 and its peers must have
 * the compositor's UID; isolated applications cannot focus arbitrary clients. */
static int mobile_control(int fd, uint32_t mask, void *data) {
    (void)mask;
    struct leon_server *server = data;
    int client = accept4(fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (client < 0)
        return 0;
    struct ucred peer;
    socklen_t length = sizeof(peer);
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &length) ||
        (peer.uid != getuid() && peer.uid != 0)) {
        close(client);
        return 0;
    }
    char command[256] = {0};
    /* Client writes before the listener is dispatched; retry asynchronously if
     * needed by using a bounded receive timeout on this one connection. */
    int flags = fcntl(client, F_GETFL);
    fcntl(client, F_SETFL, flags & ~O_NONBLOCK);
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ssize_t n = read(client, command, sizeof(command) - 1);
    if (n > 0) {
        if (!strncmp(command, "shell", 5))
            focus_toplevel(server->shell);
        else if (!strncmp(command, "focus ", 6)) {
            struct leon_toplevel *view;
            wl_list_for_each(view, &server->toplevels, link) {
                if (view->xdg_toplevel->app_id &&
                    !strcmp(view->xdg_toplevel->app_id, command + 6)) {
                    focus_toplevel(view);
                    break;
                }
            }
        }
    }
    close(client);
    return 0;
}

bool leon_control_init(struct leon_server *server) {
    server->wm_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    struct sockaddr_un control = {.sun_family = AF_UNIX};
    strcpy(control.sun_path, "/run/leon/wm.sock");
    unlink(control.sun_path);
    mode_t previous_umask = umask(0177);
    if (bind(server->wm_fd, (struct sockaddr *)&control, sizeof(control)) ||
        listen(server->wm_fd, 8)) {
        perror(control.sun_path);
        return false;
    }
    umask(previous_umask);
    server->wm_source =
        wl_event_loop_add_fd(wl_display_get_event_loop(server->wl_display), server->wm_fd,
                             WL_EVENT_READABLE, mobile_control, server);
    return true;
}
void leon_control_finish(struct leon_server *server) {
    wl_event_source_remove(server->wm_source);
    close(server->wm_fd);
    unlink("/run/leon/wm.sock");
}
