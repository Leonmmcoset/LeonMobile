#include "daemon.h"
#include "wire.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/signalfd.h>
#include <termios.h>
#include <unistd.h>
static volatile sig_atomic_t interrupted;
static void interrupt(int signal) { interrupted = signal; }
static int password(char *out, size_t size) {
    int tty = open("/dev/tty", O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (tty < 0) {
        fputs("su: a controlling terminal is required; passwords cannot be passed as arguments\n",
              stderr);
        return -1;
    }
    struct termios original, hidden;
    if (tcgetattr(tty, &original)) {
        perror("su: terminal attributes");
        close(tty);
        return -1;
    }
    hidden = original;
    hidden.c_lflag &= ~(ECHO | ECHONL);
    struct sigaction action = {.sa_handler = interrupt}, previous[5];
    int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGTSTP};
    sigemptyset(&action.sa_mask);
    for (size_t i = 0; i < 5; ++i)
        sigaction(signals[i], &action, &previous[i]);
    int result = -1;
    if (!tcsetattr(tty, TCSAFLUSH, &hidden)) {
        (void)write(tty, "Root password: ", 15);
        size_t used = 0;
        int overflow = 0;
        while (!interrupted) {
            char byte;
            ssize_t count = read(tty, &byte, 1);
            if (count <= 0) {
                if (count < 0 && !interrupted)
                    perror("su: password input");
                break;
            }
            if (byte == '\n' || byte == '\r') {
                out[used] = 0;
                result = overflow ? -1 : 0;
                break;
            }
            if (used + 1 < size)
                out[used++] = byte;
            else
                overflow = 1;
        }
        tcsetattr(tty, TCSANOW, &original);
        (void)write(tty, "\n", 1);
    }
    for (size_t i = 0; i < 5; ++i)
        sigaction(signals[i], &previous[i], NULL);
    close(tty);
    return result;
}
static int exchange(int fd, lm_json *request, char *reply, int *master) {
    char *text = lm_dump(request);
    int result = text ? root_send(fd, text, -1) : -1;
    if (text) {
        root_clear(text, strlen(text));
        free(text);
    }
    if (result || root_receive(fd, reply, LEON_ROOT_MESSAGE, master))
        return -1;
    lm_json *response = lm_parse(reply, strlen(reply));
    int ok = lm_boolean(lm_get(response, "ok"), 0);
    if (!ok)
        fprintf(stderr, "su: %s\n", lm_string(lm_get(response, "error"), "request rejected"));
    lm_free(response);
    return ok ? 0 : -1;
}
struct queue {
    char bytes[65536];
    size_t offset, size;
};
static int flush(int fd, struct queue *q) {
    ssize_t count = write(fd, q->bytes + q->offset, q->size - q->offset);
    if (count < 0)
        return errno == EINTR || errno == EAGAIN ? 0 : -1;
    q->offset += (size_t)count;
    if (q->offset == q->size)
        q->offset = q->size = 0;
    return 0;
}
static int relay(int socket, int master) {
    int tty = isatty(STDIN_FILENO), result = 1, ended = 0, drained = 0;
    struct termios original;
    if (tty) {
        if (tcgetattr(STDIN_FILENO, &original)) {
            perror("su: relay terminal attributes");
            return 1;
        }
        struct termios raw = original;
        cfmakeraw(&raw);
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw)) {
            perror("su: relay terminal mode");
            return 1;
        }
    }
    int original_flags = fcntl(STDOUT_FILENO, F_GETFL);
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
    fcntl(STDOUT_FILENO, F_SETFL, original_flags | O_NONBLOCK);
    sigset_t mask, previous;
    sigemptyset(&mask);
    for (const int *s = (int[]){SIGWINCH, SIGTERM, SIGHUP, SIGINT, SIGQUIT, 0}; *s; ++s)
        sigaddset(&mask, *s);
    sigprocmask(SIG_BLOCK, &mask, &previous);
    int signals = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    struct queue input = {0}, output = {0};
    int input_open = 1;
    for (;;) {
        struct pollfd fds[] = {
            {input_open && !input.size && !ended ? STDIN_FILENO : -1, POLLIN, 0},
            {master, (short)((!output.size && !drained ? POLLIN : 0) | (input.size ? POLLOUT : 0)),
             0},
            {ended ? -1 : socket, POLLIN, 0},
            {signals, POLLIN, 0},
            {output.size ? STDOUT_FILENO : -1, POLLOUT, 0}};
        if (poll(fds, 5, -1) < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (fds[3].revents & POLLIN) {
            struct signalfd_siginfo signal;
            while (read(signals, &signal, sizeof(signal)) == sizeof(signal)) {
                if (signal.ssi_signo == SIGWINCH) {
                    struct winsize size;
                    if (!ioctl(STDIN_FILENO, TIOCGWINSZ, &size))
                        ioctl(master, TIOCSWINSZ, &size);
                } else
                    goto done;
            }
        }
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            ssize_t count = read(STDIN_FILENO, input.bytes, sizeof(input.bytes));
            if (count > 0)
                input.size = (size_t)count;
            else if (!count)
                input_open = 0;
            else if (errno != EINTR && errno != EAGAIN)
                break;
        }
        if (input.size && fds[1].revents & POLLOUT && flush(master, &input))
            input.size = input.offset = 0;
        if (!output.size && !drained && fds[1].revents & (POLLIN | POLLHUP)) {
            ssize_t count = read(master, output.bytes, sizeof(output.bytes));
            if (count > 0)
                output.size = (size_t)count;
            else if (!count || (count < 0 && errno == EIO))
                drained = 1;
            else if (errno != EINTR && errno != EAGAIN)
                break;
        }
        if (output.size && fds[4].revents & POLLOUT && flush(STDOUT_FILENO, &output))
            break;
        if (!ended && fds[2].revents & (POLLIN | POLLHUP)) {
            char text[LEON_ROOT_MESSAGE];
            if (root_receive(socket, text, sizeof(text), NULL)) {
                result = 1;
                ended = 1;
            } else {
                lm_json *reply = lm_parse(text, strlen(text));
                result = (int)lm_integer(lm_get(reply, "status"), 1);
                lm_free(reply);
                ended = 1;
            }
        }
        if (ended && drained && !output.size)
            break;
    }
done:
    if (tty)
        tcsetattr(STDIN_FILENO, TCSANOW, &original);
    fcntl(STDOUT_FILENO, F_SETFL, original_flags);
    if (signals >= 0)
        close(signals);
    sigprocmask(SIG_SETMASK, &previous, NULL);
    root_clear(&input, sizeof(input));
    return result;
}
int main(int argc, char **argv) {
    const char *command = "";
    int status_only = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--status"))
            status_only = 1;
        else if (!strcmp(argv[i], "-c") && i + 1 < argc)
            command = argv[++i];
        else if (strcmp(argv[i], "-") && strcmp(argv[i], "root")) {
            fputs("usage: su [-] [-c COMMAND] [root] | su --status\n", stderr);
            return 2;
        }
    }
    struct rlimit core = {0, 0};
    setrlimit(RLIMIT_CORE, &core);
    prctl(PR_SET_DUMPABLE, 0);
    const char *path = getenv("LEON_ROOT_SOCKET");
    int socket = root_connect(path ? path : LEON_ROOT_SOCKET);
    if (socket < 0) {
        perror("su: root service");
        return 1;
    }
    char reply[LEON_ROOT_MESSAGE];
    lm_json *request = lm_object();
    lm_set_string(request, "op", "status");
    if (exchange(socket, request, reply, NULL)) {
        lm_free(request);
        close(socket);
        return 1;
    }
    lm_json *state = lm_parse(reply, strlen(reply));
    int enabled = lm_boolean(lm_get(state, "enabled"), 0);
    lm_free(state);
    close(socket);
    if (status_only) {
        fputs(reply, stdout);
        lm_free(request);
        return 0;
    }
    if (!enabled) {
        fputs("su: root access is disabled; enable it in Settings > System > Developer options\n",
              stderr);
        lm_free(request);
        return 1;
    }
    char secret[129] = {0};
    if (password(secret, sizeof(secret))) {
        root_clear(secret, sizeof(secret));
        lm_free(request);
        return 1;
    }
    socket = root_connect(path ? path : LEON_ROOT_SOCKET);
    if (socket < 0) {
        perror("su: root service");
        root_clear(secret, sizeof(secret));
        lm_free(request);
        return 1;
    }
    struct winsize size = {24, 80, 0, 0};
    ioctl(STDIN_FILENO, TIOCGWINSZ, &size);
    lm_set_string(request, "op", "exec");
    lm_set_string(request, "password", secret);
    lm_set_string(request, "command", command);
    lm_set_integer(request, "rows", size.ws_row ? size.ws_row : 24);
    lm_set_integer(request, "columns", size.ws_col ? size.ws_col : 80);
    root_clear(secret, sizeof(secret));
    int master = -1;
    int result = exchange(socket, request, reply, &master);
    if (result)
        fputs("su: root session request failed\n", stderr);
    char *stored = (char *)lm_string(lm_get(request, "password"), "");
    root_clear(stored, strlen(stored));
    lm_free(request);
    if (!result && master >= 0)
        result = relay(socket, master);
    else
        result = 1;
    if (master >= 0)
        close(master);
    close(socket);
    return result;
}
