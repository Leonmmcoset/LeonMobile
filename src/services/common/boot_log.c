#include "boot_log.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static bool console_enabled;

void leon_boot_console_enable(void) { console_enabled = true; }

void leon_boot_log(const char *format, ...) {
    int saved_errno = errno;
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    vfprintf(stderr, format, args);
    va_end(args);
    if (console_enabled) {
        char message[1024];
        int length = vsnprintf(message, sizeof(message), format, copy);
        int fd = open("/dev/tty1", O_WRONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK | O_NOFOLLOW);
        int mode;
        /* seatd owns KD_GRAPHICS while Wayland runs. Never draw over its scene.
         * Missing or busy VTs must not delay boot or suppress serial errors. */
        if (fd >= 0 && length > 0 && !ioctl(fd, KDGETMODE, &mode) && mode == KD_TEXT) {
            size_t count = (size_t)length < sizeof(message) ? (size_t)length : sizeof(message) - 1;
            ssize_t ignored = write(fd, message, count);
            (void)ignored;
        }
        if (fd >= 0)
            close(fd);
    }
    va_end(copy);
    errno = saved_errno;
}

void leon_boot_error(const char *label) {
    int saved_errno = errno;
    leon_boot_log("%s: %s\n", label, strerror(saved_errno));
    errno = saved_errno;
}
