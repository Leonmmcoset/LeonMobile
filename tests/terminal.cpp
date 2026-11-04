#include "input.hpp"
#include "session.hpp"
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>
using leon::terminal::Screen;
using leon::terminal::Session;
using namespace std::chrono_literals;
static void screen_tests() {
    Screen screen(6, 20);
    screen.feed("\033[2J\033[H\033[1;3;4;9;31;44mA \033[0mB");
    auto cell = screen.cell(0, 0);
    assert(cell.chars[0] == 'A' && cell.attrs.bold && cell.attrs.italic && cell.attrs.underline &&
           cell.attrs.strike);
    auto fg = screen.rgb(cell.fg), bg = screen.rgb(cell.bg);
    assert(fg.rgb.red > fg.rgb.blue && bg.rgb.blue > bg.rgb.red);
    assert(screen.rgb(screen.cell(0, 1).bg).rgb.blue ==
           bg.rgb.blue); // Spaces carry SGR backgrounds.
    screen.feed("\033[2;3H\033[38;2;12;34;56;48;5;196mZ");
    cell = screen.cell(1, 2);
    fg = screen.rgb(cell.fg);
    bg = screen.rgb(cell.bg);
    assert(fg.rgb.red == 12 && fg.rgb.green == 34 && fg.rgb.blue == 56 && bg.rgb.red == 255);
    screen.feed("\033[0m\033[3;1H12345\033[3;3H\033[2@XY");
    assert(screen.selection({2, 0}, {2, 6}) == "12XY345");
    screen.feed("\033[3;3H\033[2P");
    assert(screen.selection({2, 0}, {2, 4}) == "12345");
    screen.feed("\033[4;8H\0337\033[H\0338\033[6n");
    assert(screen.take_output() == "\033[4;8R");
    screen.feed("\033[?25l");
    assert(!screen.cursor_visible());
    screen.feed("\033[?25h\033[5 q");
    assert(screen.cursor_visible());
    screen.feed("\033[?1049h\033[Halternate");
    assert(screen.alternate() && screen.selection({0, 0}, {0, 8}) == "alternate");
    screen.feed("\033[?1049l");
    assert(!screen.alternate() && screen.cell(0, 0).chars[0] == 'A');
    screen.reset();
    const std::string unicode = "中é😀";
    for (char byte : unicode)
        screen.feed(std::string_view(&byte, 1));
    assert(screen.cell(0, 0).chars[0] == 0x4e2d && screen.cell(0, 0).width == 2);
    assert(screen.cell(0, 2).chars[0] == 'e' && screen.cell(0, 2).chars[1] == 0x301);
    assert(screen.cell(0, 3).chars[0] == 0x1f600 && screen.cell(0, 3).width == 2);
    assert(screen.selection({0, 0}, {0, 4}) == unicode);
    screen.feed("\r\n\033(0lqqk\033(B");
    assert(screen.cell(1, 1).chars[0] == 0x2500);
    screen.reset();
    screen.feed("\033[31");
    screen.feed("mX");
    assert(screen.cell(0, 0).chars[0] == 'X');
    screen.feed("\033]2;bounded title\007");
    assert(screen.title() == "bounded title");
    screen.key(VTERM_KEY_UP);
    assert(screen.take_output() == "\033[A");
    screen.feed("\033[?1h");
    screen.key(VTERM_KEY_UP);
    assert(screen.take_output() == "\033OA");
    leon::terminal::key_input(screen, XKB_KEY_c, VTERM_MOD_CTRL);
    assert(screen.take_output() == "\003");
    leon::terminal::key_input(screen, XKB_KEY_x, VTERM_MOD_ALT);
    assert(screen.take_output() == "\033x");
    screen.key(VTERM_KEY_TAB, VTERM_MOD_SHIFT);
    assert(screen.take_output() == "\033[Z");
    screen.key(VTermKey(VTERM_KEY_FUNCTION(5)));
    assert(screen.take_output() == "\033[15~");
    screen.key(VTERM_KEY_UP, VTERM_MOD_CTRL);
    assert(screen.take_output() == "\033[1;5A");
    screen.feed("\033[?2004h");
    screen.paste("a\nb\033[201~");
    assert(screen.take_output() == "\033[200~a\rb[201~\033[201~");
    screen.feed("\033[?1000h\033[?1006h");
    assert(screen.mouse_mode());
    screen.mouse(2, 3, 1, true);
    assert(screen.take_output() == "\033[<0;4;3M");
    screen.mouse(2, 3, 1, false);
    assert(screen.take_output() == "\033[<0;4;3m");
    screen.feed("\033[?1004h");
    screen.focus(true);
    assert(screen.take_output() == "\033[I");
    screen.focus(false);
    assert(screen.take_output() == "\033[O");
    screen.reset();
    for (int i = 0; i < 2200; ++i)
        screen.feed("history\r\n");
    assert(screen.history_size() == 2000);
    screen.scroll(5000);
    assert(screen.offset() == 2000);
    assert(screen.selection({0, 0}, {0, 6}) == "history");
    screen.resize(8, 30);
    assert(screen.rows() == 8 && screen.columns() == 30);
    screen.live();
    screen.feed("\033[?1049h");
    size_t before = screen.history_size();
    for (int i = 0; i < 30; ++i)
        screen.feed("alternate\r\n");
    assert(screen.history_size() == before);
    screen.feed("\033[?1049l");
    puts("PASS terminal ANSI/VT state, colors, Unicode, modes, replies, input and bounded history");
}
static uint64_t hash(std::string_view bytes, uint64_t value = 1469598103934665603ULL) {
    for (unsigned char byte : bytes)
        value = (value ^ byte) * 1099511628211ULL;
    return value;
}
static int child() {
    termios attributes{};
    assert(!tcgetattr(0, &attributes));
    cfmakeraw(&attributes);
    assert(!tcsetattr(0, TCSANOW, &attributes));
    winsize size{};
    assert(!ioctl(0, TIOCGWINSZ, &size));
    assert(isatty(0) && tcgetsid(0) == getsid(0));
    assert(fcntl(99, F_GETFD) == -1 && errno == EBADF);
    assert(!strcmp(getenv("TERM"), "xterm-256color") && !strcmp(getenv("COLORTERM"), "truecolor"));
    printf("READY %u %u\n", size.ws_row, size.ws_col);
    fflush(stdout);
    usleep(250000);
    size_t received = 0;
    uint64_t value = 1469598103934665603ULL;
    char buffer[8192];
    while (received < 512 * 1024) {
        ssize_t count = read(0, buffer, std::min(sizeof(buffer), 512 * 1024 - received));
        if (count < 0 && errno == EINTR)
            continue;
        assert(count > 0);
        value = hash(std::string_view(buffer, size_t(count)), value);
        received += size_t(count);
    }
    assert(!ioctl(0, TIOCGWINSZ, &size));
    printf("DONE %zu %llu %u %u\n", received, (unsigned long long)value, size.ws_row, size.ws_col);
    return 7;
}
static void pty_tests(const char *executable) {
    int leak = open("/dev/null", O_RDONLY);
    assert(leak >= 0 && dup2(leak, 99) == 99);
    close(leak);
    std::string output;
    Session session([&](std::string_view bytes) { output += bytes; }, {executable, "--child"});
    session.start(20, 72);
    close(99);
    auto until = [&](auto predicate) {
        auto deadline = std::chrono::steady_clock::now() + 8s;
        while (!predicate() && std::chrono::steady_clock::now() < deadline) {
            session.pump();
            pollfd fd{session.fd(), short(POLLIN | (session.wants_write() ? POLLOUT : 0)), 0};
            poll(&fd, 1, 10);
        }
        assert(predicate());
    };
    until([&] { return output.find("READY 20 72") != std::string::npos; });
    session.resize(31, 92);
    std::string payload(512 * 1024, 'x');
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = char(i % 251);
    assert(session.send(payload) && session.wants_write());
    assert(!session.send(std::string(1024 * 1024, 'y')));
    until([&] { return !session.running(); });
    for (int i = 0; i < 5; ++i)
        session.pump();
    assert(session.status() == 7);
    std::string expected = "DONE 524288 " + std::to_string(hash(payload)) + " 31 92";
    assert(output.find(expected) != std::string::npos);
    assert(!session.wants_write());
    Screen screen(24, 80);
    Session shell([&](std::string_view bytes) {
        screen.feed(bytes);
        output += bytes;
    });
    output.clear();
    shell.start(24, 80);
    auto wait_text = [&](std::string_view text) {
        auto deadline = std::chrono::steady_clock::now() + 5s;
        while (output.find(text) == std::string::npos &&
               std::chrono::steady_clock::now() < deadline) {
            shell.pump();
            auto response = screen.take_output();
            if (!response.empty())
                shell.send(response);
            usleep(5000);
        }
        assert(output.find(text) != std::string::npos);
    };
    assert(shell.send("printf 'READY_%s\\n' SHELL\r"));
    wait_text("READY_SHELL");
    assert(shell.send("sleep 60\r"));
    for (int i = 0; i < 50; ++i) {
        shell.pump();
        usleep(5000);
    }
    screen.character('c', VTERM_MOD_CTRL);
    assert(shell.send(screen.take_output()));
    assert(shell.send("printf 'AFTER_%s\\n' INTERRUPT\r"));
    wait_text("AFTER_INTERRUPT");
    assert(shell.running());
    shell.stop();
    assert(!shell.running() && shell.fd() < 0);
    puts("PASS terminal real PTY, controlling session, 512 KiB backpressure, resize, shell and "
         "Ctrl-C job control");
}
int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--child"))
        return child();
    screen_tests();
    pty_tests(argv[0]);
}
