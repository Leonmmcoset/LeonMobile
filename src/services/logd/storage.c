#include "logd.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
int log_secure_directory(const char *path, mode_t mode) {
    mode_t previous = umask(0);
    int result = lm_mkdir(path, mode);
    umask(previous);
    if (result)
        return -1;
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || st.st_uid || (st.st_mode & 0777) != mode) {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    return fd;
}
void log_child(int fd, pid_t parent) {
    if (lm_child_setup(parent))
        _exit(126);
    if (fd != 3) {
        if (dup3(fd, 3, O_CLOEXEC) < 0)
            _exit(126);
    }
    /* Workers cannot retain listeners, locks, other clients or notification FDs. */
    if (syscall(SYS_close_range, 4u, ~0u, 0))
        for (int i = 4; i < 65536; ++i)
            close(i);
}
static int regular(int fd) {
    struct stat st;
    return fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode) && !st.st_uid &&
           (st.st_mode & 0777) == 0600 && st.st_nlink == 1 &&
           st.st_size <= LOG_SEGMENT_BYTES;
}
static int journal_open(int dir) {
    int fd = openat(dir, "journal.0", O_RDWR | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW |
                                        O_NONBLOCK, 0600);
    if (!regular(fd)) {
        if (fd >= 0)
            close(fd);
        errno = EPERM;
        return -1;
    }
    /* Recover only the incomplete last record. No scan of an unbounded file. */
    off_t end = lseek(fd, 0, SEEK_END);
    if (end > 0) {
        char tail[LOG_TEXT];
        off_t start = end > (off_t)sizeof(tail) ? end - (off_t)sizeof(tail) : 0;
        ssize_t n = pread(fd, tail, (size_t)(end - start), start);
        if (n != end - start)
            goto fail;
        if (tail[n - 1] != '\n') {
            while (n && tail[n - 1] != '\n')
                --n;
            if (ftruncate(fd, n ? start + n : 0))
                goto fail;
        }
    }
    return fd;
fail:
    close(fd);
    return -1;
}
static int rotate(int dir, int *fd) {
    if (fsync(*fd))
        return -1;
    close(*fd);
    *fd = -1;
    if (unlinkat(dir, "journal.3", 0) && errno != ENOENT)
        return -1;
    for (int i = LOG_SEGMENTS - 2; i >= 0; --i) {
        char from[32], to[32];
        snprintf(from, sizeof(from), "journal.%d", i);
        snprintf(to, sizeof(to), "journal.%d", i + 1);
        if (renameat(dir, from, dir, to) && errno != ENOENT)
            return -1;
    }
    *fd = journal_open(dir);
    return *fd < 0 || fsync(dir) ? -1 : 0;
}
void log_writer(const struct log_options *options, int socket) {
    int dir = -1, fd = -1, error = 0;
    int64_t retry = 0, synced = 0;
    struct log_ack ack = {0};
    for (;;) {
        int64_t now = log_clock_ns(CLOCK_MONOTONIC) / 1000000;
        struct pollfd p = {socket, POLLIN, 0};
        int ready = poll(&p, 1, 500);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0 || p.revents & (POLLHUP | POLLERR | POLLNVAL))
            break;
        char text[LOG_TEXT];
        ssize_t n = 0;
        if (p.revents & POLLIN) {
            n = recv(socket, text, sizeof(text), MSG_DONTWAIT);
            if (!n)
                break;
        }
        if (fd < 0 && now >= retry) {
            if (dir >= 0)
                close(dir);
            dir = log_secure_directory(options->state, 0700);
            fd = dir >= 0 ? journal_open(dir) : -1;
            error = fd < 0 ? (errno ? errno : EACCES) : 0;
            retry = now + 1000;
        }
        if (n > 0 && n < LOG_TEXT && text[n - 1] == '\n') {
            int saved = 0;
            if (fd >= 0) {
                off_t before = lseek(fd, 0, SEEK_END);
                if (before < 0 || (before + n > LOG_SEGMENT_BYTES && rotate(dir, &fd)))
                    error = errno ? errno : EIO;
                else {
                    before = lseek(fd, 0, SEEK_END);
                    ssize_t written;
                    do {
                        written = write(fd, text, (size_t)n);
                    } while (written < 0 && errno == EINTR);
                    if (written != n) {
                        error = errno ? errno : ENOSPC;
                        (void)ftruncate(fd, before);
                    } else {
                        lm_json *j = lm_parse(text, (size_t)n);
                        ack.seq = lm_integer(lm_get(j, "seq"), ack.seq);
                        lm_free(j);
                        ack.bytes += n;
                        saved = 1;
                    }
                }
            }
            if (!saved)
                ++ack.dropped;
        }
        if (fd >= 0 && now - synced >= 1000) {
            if (fsync(fd))
                error = errno ? errno : EIO;
            synced = now;
        }
        if (error && fd >= 0) {
            close(fd);
            fd = -1;
            retry = now + 1000;
        }
        ack.error = error;
        (void)send(socket, &ack, sizeof(ack), MSG_DONTWAIT | MSG_NOSIGNAL);
    }
    if (fd >= 0) {
        (void)fsync(fd);
        close(fd);
    }
    if (dir >= 0)
        close(dir);
}
int log_storage_scan(const char *path, void (*visit)(void *, const char *, size_t), void *context) {
    int dir = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat st;
    if (dir < 0)
        return -1;
    if (fstat(dir, &st) || st.st_uid || (st.st_mode & 0777) != 0700) {
        close(dir);
        return -1;
    }
    int degraded = 0;
    for (int i = LOG_SEGMENTS - 1; i >= 0; --i) {
        char name[32];
        snprintf(name, sizeof(name), "journal.%d", i);
        int fd = openat(dir, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (fd < 0) {
            if (errno != ENOENT)
                degraded = 1;
            continue;
        }
        if (!regular(fd)) {
            degraded = 1;
            close(fd);
            continue;
        }
        char line[LOG_TEXT];
        size_t used = 0, total = 0;
        int oversize = 0;
        char buffer[8192];
        ssize_t n;
        while (total < LOG_SEGMENT_BYTES && (n = read(fd, buffer, sizeof(buffer))) > 0) {
            total += (size_t)n;
            for (ssize_t j = 0; j < n; ++j) {
                char c = buffer[j];
                if (c == '\n') {
                    if (!oversize && used)
                        visit(context, line, used);
                    used = 0;
                    oversize = 0;
                } else if (used < sizeof(line))
                    line[used++] = c;
                else
                    oversize = 1;
            }
        }
        close(fd);
    }
    close(dir);
    return degraded ? -1 : 0;
}
