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
    lmui::Application committed("org.leon.commit-test", "Surface commit ordering");
    int paints = 0, callbacks = 0, idle_ticks = 0;
    committed.paint = [&](lmui::Canvas &) { ++paints; };
    committed.after_commit([&] { assert(false && "superseded callback must be cancelled"); });
    committed.after_commit([&] {
        assert(paints > 0 && callbacks == 0);
        ++callbacks;
        int previous = paints;
        committed.after_commit([&, previous] {
            assert(paints > previous && callbacks == 1);
            ++callbacks;
            committed.quit();
        });
    });
    committed.tick = [&] { assert(++idle_ticks < 10 && "commit acknowledgement timed out"); };
    assert(!committed.run());
    assert(callbacks == 2);
    return 0;
}
