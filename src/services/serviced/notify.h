#ifndef LEON_SERVICE_NOTIFY_H
#define LEON_SERVICE_NOTIFY_H
/* Internal supervised-service transport. Only the foreground main PID may
 * send readiness/watchdog messages; descendants must not keep it across exec. */
#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
static inline int leon_service_message(const char *text, size_t size) {
    const char *value = getenv("LEON_NOTIFY_FD");
    if (!value)
        return 0;
    char *end;
    long number = strtol(value, &end, 10);
    if (*end || number < 3 || number > 1048576)
        return -1;
    int fd = (int)number;
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC))
        return -1;
    return send(fd, text, size, MSG_DONTWAIT | MSG_NOSIGNAL) == (ssize_t)size ? 0 : -1;
}
static inline int leon_service_ready(void) { return leon_service_message("READY=1", 7); }
static inline int leon_service_watchdog(void) { return leon_service_message("WATCHDOG=1", 10); }
#endif
