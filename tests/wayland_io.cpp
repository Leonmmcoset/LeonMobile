#include <cassert>
#include <fcntl.h>
#include <lmui/lmui.hpp>
#include <unistd.h>
int main() {
    int fds[2];
    assert(!pipe2(fds, O_NONBLOCK | O_CLOEXEC));
    lmui::Application app("org.leon.io-test", "FD reactor test");
    int calls = 0, ticks = 0;
    app.tick = [&] { ++ticks; };
    app.watch_fd(fds[0], [&] {
        char data[8];
        assert(read(fds[0], data, sizeof(data)) == 3);
        ++calls;
        app.unwatch_fd(fds[0]);
        app.quit();
    });
    assert(write(fds[1], "ime", 3) == 3);
    int writable = 0;
    app.watch_fd(fds[1], lmui::fd_writable, [&] {
        ++writable;
        app.unwatch_fd(fds[1]);
    });
    assert(!app.run());
    assert(calls == 1 && ticks == 0);
    assert(writable == 1);
    close(fds[0]);
    close(fds[1]);
    return 0;
}
