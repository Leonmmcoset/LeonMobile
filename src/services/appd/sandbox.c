#define _GNU_SOURCE
#include "sandbox.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Landlock ABI 1 structures/constants, kept local because the pinned musl SDK
 * has older UAPI headers. These exact ABI definitions are specified by Linux. */
struct ruleset {
    uint64_t access;
};
struct path_rule {
    uint64_t access;
    int32_t parent;
} __attribute__((packed));
#define LL_CREATE 444
#define LL_ADD 445
#define LL_RESTRICT 446
#define FS_EXEC (1ULL << 0)
#define FS_WRITE (1ULL << 1)
#define FS_READ (1ULL << 2)
#define FS_DIR (1ULL << 3)
#define FS_ALL ((1ULL << 13) - 1)
static int rule(int rules, const char *path, uint64_t access) {
    int fd = open(path, O_PATH | O_CLOEXEC);
    if (fd < 0)
        return errno == ENOENT ? 0 : -1;
    struct stat info;
    if (fstat(fd, &info)) {
        close(fd);
        return -1;
    }
    if (!S_ISDIR(info.st_mode))
        access &= FS_EXEC | FS_WRITE | FS_READ;
    struct path_rule rule = {access, fd};
    int result = (int)syscall(LL_ADD, rules, 1, &rule, 0);
    close(fd);
    return result;
}
static int filter(int network) {
#if defined(__aarch64__)
#define LEON_ARCH AUDIT_ARCH_AARCH64
#elif defined(__x86_64__)
#define LEON_ARCH AUDIT_ARCH_X86_64
#else
#error Unsupported sandbox architecture
#endif
#define DENY(n)                                                                                    \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (n), 0, 1),                                                \
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM)
    struct sock_filter instructions[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, LEON_ARCH, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        /* io_uring can create sockets outside socket(2)'s seccomp check. */
        DENY(425), /* io_uring_setup */
        DENY(426), /* io_uring_enter */
        DENY(427), /* io_uring_register */
        /* Reject x32 ABI numbers even when the audit architecture is x86_64. */
        BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x40000000, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS), DENY(__NR_ptrace), DENY(__NR_mount),
        DENY(__NR_umount2), DENY(__NR_unshare), DENY(__NR_setns), DENY(__NR_bpf),
        DENY(__NR_perf_event_open), DENY(__NR_keyctl), DENY(__NR_reboot), DENY(__NR_kexec_load),
        DENY(__NR_init_module), DENY(__NR_finit_module), DENY(__NR_delete_module),
        DENY(__NR_userfaultfd),
#ifdef __NR_open_by_handle_at
        DENY(__NR_open_by_handle_at),
#endif
#ifdef __NR_clone3
        /* libc may fall back to clone; clone3's pointed-to flags cannot be
         * inspected safely by classic seccomp. */
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clone3, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS),
#endif
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clone, 0, 4),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x7e020000, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_socket, 0, 6),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_UNIX, 4, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, network ? AF_INET : AF_UNIX, 3, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, network ? AF_INET6 : AF_UNIX, 2, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EACCES),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EACCES),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)};
    struct sock_fprog program = {sizeof(instructions) / sizeof(instructions[0]), instructions};
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program);
}
int leon_sandbox(const char *home, const char *runtime, int network) {
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
        return -1;
    int abi = (int)syscall(LL_CREATE, NULL, 0, 1);
    if (abi < 3) {
        errno = ENOTSUP;
        return -1;
    }
    uint64_t all = FS_ALL | (1ULL << 13) | (1ULL << 14);
    struct ruleset set = {all};
    int rules = (int)syscall(LL_CREATE, &set, sizeof(set), 0);
    if (rules < 0)
        return -1;
    const char *readable[] = {
        "/bin", "/sbin",           "/usr",      "/lib", "/lib64", "/etc", "/proc",
        "/sys", "/data/leon/apps", "/run/leon", NULL};
    int failed = 0;
    for (int i = 0; readable[i]; ++i)
        if (rule(rules, readable[i], FS_READ | FS_DIR | FS_EXEC))
            failed = 1;
    if (rule(rules, home, all) || rule(rules, runtime, all) ||
        rule(rules, "/dev/null", FS_READ | FS_WRITE) ||
        rule(rules, "/dev/zero", FS_READ | FS_WRITE) || rule(rules, "/dev/urandom", FS_READ) ||
        rule(rules, "/dev/tty", FS_READ | FS_WRITE) ||
        rule(rules, "/dev/random", FS_READ) || rule(rules, "/dev/ptmx", FS_READ | FS_WRITE) ||
        rule(rules, "/dev/pts", FS_READ | FS_WRITE | FS_DIR))
        failed = 1;
    if (failed || syscall(LL_RESTRICT, rules, 0)) {
        close(rules);
        return -1;
    }
    close(rules);
    struct rlimit cores = {8 * 1024 * 1024, 8 * 1024 * 1024};
    if (setrlimit(RLIMIT_CORE, &cores))
        return -1;
    struct rlimit files = {512, 512};
    if (setrlimit(RLIMIT_NOFILE, &files))
        return -1;
    return filter(network);
}
static int write_value(const char *path, const char *text) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    int result = write(fd, text, strlen(text)) == (ssize_t)strlen(text) ? 0 : -1;
    close(fd);
    return result;
}
int leon_cgroup(pid_t pid, uid_t uid) {
    char directory[256], path[300], value[32];
    snprintf(directory, sizeof(directory), "/sys/fs/cgroup/leon/apps/%u", uid);
    if (mkdir(directory, 0755) && errno != EEXIST)
        return -1;
    snprintf(path, sizeof(path), "%s/memory.max", directory);
    if (write_value(path, "268435456"))
        return -1;
    snprintf(path, sizeof(path), "%s/memory.oom.group", directory);
    if (write_value(path, "1"))
        return -1;
    snprintf(path, sizeof(path), "%s/pids.max", directory);
    if (write_value(path, "128"))
        return -1;
    snprintf(path, sizeof(path), "%s/cgroup.procs", directory);
    snprintf(value, sizeof(value), "%d", pid);
    return write_value(path, value);
}

int leon_cgroup_kill(uid_t uid) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/fs/cgroup/leon/apps/%u/cgroup.kill", uid);
    int result = write_value(path, "1");
    return result && errno == ENOENT ? 0 : result;
}

int leon_cgroup_reset(void) {
    DIR *directory = opendir("/sys/fs/cgroup/leon/apps");
    if (!directory)
        return -1;
    struct dirent *entry;
    int result = 0;
    while ((entry = readdir(directory))) {
        char *end;
        unsigned long uid = strtoul(entry->d_name, &end, 10);
        if (*entry->d_name && !*end && uid >= 11000 && uid <= 60000 && leon_cgroup_kill((uid_t)uid))
            result = -1;
    }
    closedir(directory);
    return result;
}
