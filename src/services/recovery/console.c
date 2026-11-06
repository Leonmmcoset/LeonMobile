#include "recovery.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
int recovery_console_open(struct recovery *r) {
    r->console[0] = open("/dev/tty1", O_RDWR | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
    r->console[1] = open("/dev/ttyAMA0", O_RDWR | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
    if (r->console[0] >= 0) {
        ioctl(r->console[0], KDSETMODE, KD_TEXT);
        ioctl(r->console[0], KDSKBMODE, K_XLATE);
        ioctl(r->console[0], VT_ACTIVATE, 1);
        ioctl(r->console[0], VT_WAITACTIVE, 1);
    }
    for (int i = 0; i < 2; ++i) if (r->console[i] >= 0) {
        struct termios t;
        if (!tcgetattr(r->console[i], &t)) {
            t.c_lflag &= ~(ECHO | ICANON | ISIG); t.c_iflag &= ~(IXON | ICRNL);
            t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
            tcsetattr(r->console[i], TCSANOW, &t);
        }
    }
    return r->console[0] < 0 && r->console[1] < 0 ? -1 : 0;
}
void recovery_draw(struct recovery *r, const char *page) {
    char text[4096];
    snprintf(text, sizeof(text),
        "\033[?25l\033[0m\033[44m\033[2J\033[H\n"
        "  LEONMOBILE\n  Recovery 1.0.0\n"
        "  ----------------------------------------\n\n"
        "  Independent recovery environment\n"
        "  %s\n\n"
        "  Boot attempts: %-5u  Failed: %u\n"
        "  Data partition: %s\n\n"
        "  %s\n\n"
        "  ----------------------------------------\n\n"
        "  [1] Boot normally\n\n"
        "  [2] Boot in safe mode\n\n"
        "  [3] Check system filesystem\n\n"
        "  [4] Restore verified system image\n"
        "      Keep applications and personal data\n\n"
        "  [5] View boot diagnostics\n\n"
        "  [6] Export diagnostics (Root password)\n\n"
        "  [7] Power off\n\n"
        "  ----------------------------------------\n\n"
        "  %s\n\n"
        "  Use keyboard / serial console.\n  Choose an option, then press Enter.\n\033[?25h  > ",
        leon_boot_reason_name(r->reason), r->state.attempts, r->state.failures,
        r->data_available ? "available; preserved" : "unavailable; no data writes",
        page, r->message);
    for (int i = 0; i < 2; ++i) if (r->console[i] >= 0) (void)write(r->console[i], text, strlen(text));
}
int recovery_line(struct recovery *r, char *text, unsigned capacity, int secret) {
    if (!capacity) return -1;
    unsigned used = 0; int active = -1;
    memset(text, 0, capacity);
    for (;;) {
        struct pollfd fds[] = {{r->console[0], POLLIN, 0}, {r->console[1], POLLIN, 0}};
        if (poll(fds, 2, -1) < 0) { if (errno == EINTR) continue; return -1; }
        for (int i = 0; i < 2; ++i) if (fds[i].revents & POLLIN) {
            unsigned char c;
            if (read(fds[i].fd, &c, 1) != 1) continue;
            if (active >= 0 && active != i) continue;
            if (c == '\r' || c == '\n') { text[used] = 0; return (int)used; }
            if (c == 27 || c == 3) { memset(text, 0, capacity); return -1; }
            active = i;
            if (c == 127 || c == 8) { if (used) { text[--used] = 0; if (!secret) (void)write(fds[i].fd, "\b \b", 3); } }
            else if (c >= 32 && c < 127 && used + 1 < capacity) {
                text[used++] = (char)c;
                if (!secret) (void)write(fds[i].fd, &c, 1);
            }
        }
    }
}
