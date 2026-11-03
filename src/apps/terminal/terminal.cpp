#include "terminal.hpp"
#include <csignal>
#include <fcntl.h>
#include <pty.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace mobile;
namespace leon::apps {
Terminal::Terminal() {
    vt = vterm_new(rows, cols);
    vterm_set_utf8(vt, 1);
    screen = vterm_obtain_screen(vt);
    vterm_screen_reset(screen, 1);
    winsize ws{static_cast<unsigned short>(rows), static_cast<unsigned short>(cols), 0, 0};
    child = forkpty(&fd, nullptr, nullptr, &ws);
    if (!child) {
        setenv("TERM", "xterm-256color", 1);
        chdir(user_home().c_str());
        execl("/bin/sh", "sh", (char *)nullptr);
        _exit(127);
    }
    if (fd >= 0)
        fcntl(fd, F_SETFL, O_NONBLOCK);
}
Terminal::~Terminal() {
    if (child > 0) {
        kill(child, SIGHUP);
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
    }
    if (fd >= 0)
        close(fd);
    if (vt)
        vterm_free(vt);
}
void Terminal::poll() {
    if (fd < 0)
        return;
    char b[4096];
    ssize_t n;
    while ((n = read(fd, b, sizeof(b))) > 0)
        vterm_input_write(vt, b, size_t(n));
    vterm_screen_flush_damage(screen);
}
void Terminal::input(const std::string &s) {
    if (fd >= 0)
        write(fd, s.data(), s.size());
}
void Terminal::draw(Canvas &c, Rect r) {
    poll();
    c.rounded(r, Color::hex(0x19151f), 16);
    double cw = 8.5, ch = 19;
    int nc = std::max(8, int((r.w - 20) / cw)), nr = std::max(4, int((r.h - 20) / ch));
    if (nc != cols || nr != rows) {
        cols = nc;
        rows = nr;
        vterm_set_size(vt, rows, cols);
        winsize ws{static_cast<unsigned short>(rows), static_cast<unsigned short>(cols), 0, 0};
        ioctl(fd, TIOCSWINSZ, &ws);
    }
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols; ++x) {
            VTermScreenCell cell{};
            vterm_screen_get_cell(screen, {y, x}, &cell);
            if (!cell.chars[0])
                continue;
            char out[8]{};
            uint32_t u = cell.chars[0];
            int len = 0;
            if (u < 128)
                out[len++] = char(u);
            else if (u < 2048) {
                out[len++] = char(0xc0 | (u >> 6));
                out[len++] = char(0x80 | (u & 63));
            } else if (u < 65536) {
                out[len++] = char(0xe0 | (u >> 12));
                out[len++] = char(0x80 | ((u >> 6) & 63));
                out[len++] = char(0x80 | (u & 63));
            } else {
                out[len++] = char(0xf0 | (u >> 18));
                out[len++] = char(0x80 | ((u >> 12) & 63));
                out[len++] = char(0x80 | ((u >> 6) & 63));
                out[len++] = char(0x80 | (u & 63));
            }
            c.text(std::string(out, len), {r.x + 10 + x * cw, r.y + 10 + y * ch, cw * 2, ch}, 14,
                   Color::hex(0xeee6f4));
        }
}
} // namespace leon::apps
