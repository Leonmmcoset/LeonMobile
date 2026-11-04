#include "backend.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
namespace lmui {
void Application::Impl::lifecycle_open() {
    const char *value = getenv("LEON_LIFECYCLE_FD");
    if (!value)
        return;
    char *end;
    long fd = strtol(value, &end, 10);
    if (*end || fd < 3 || fd > 1048576)
        return;
    int flags = fcntl(int(fd), F_GETFD);
    if (flags < 0 || fcntl(int(fd), F_SETFD, flags | FD_CLOEXEC))
        return;
    lifecycle_fd = int(fd);
}
void Application::Impl::lifecycle_dispatch() {
    char text[64];
    for (int i = 0; i < 8; ++i) {
        ssize_t count = recv(lifecycle_fd, text, sizeof(text) - 1, MSG_DONTWAIT);
        if (count <= 0)
            return;
        text[count] = 0;
        unsigned sequence;
        char event[24], extra;
        if (sscanf(text, "%u %23s %c", &sequence, event, &extra) != 2)
            continue;
        LifecycleEvent kind;
        if (!strcmp(event, "foreground"))
            kind = LifecycleEvent::foreground;
        else if (!strcmp(event, "background"))
            kind = LifecycleEvent::background;
        else if (!strcmp(event, "save"))
            kind = LifecycleEvent::save;
        else if (!strcmp(event, "shutdown"))
            kind = LifecycleEvent::shutdown;
        else
            continue;
        bool ok = true;
        try {
            if (lifecycle_callback)
                ok = lifecycle_callback(kind);
        } catch (...) {
            ok = false;
        }
        auto reply = std::to_string(sequence) + (ok ? " ok" : " failed");
        send(lifecycle_fd, reply.data(), reply.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
        if (kind == LifecycleEvent::shutdown)
            running = false;
    }
}
} // namespace lmui
