#define _GNU_SOURCE
#include "system_probe.h"
#include "../../src/services/boot/state.h"
#include <errno.h>
#include <fcntl.h>
#include <mntent.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/reboot.h>
#include <unistd.h>

#define ROOT_PASSWORD "RecoveryTest!2026"
#define FIXTURE_PACKAGE "/usr/share/recovery-demo.lmpkg"
#define OVERRIDDEN_SERVICE "memory"
#define STATE_FILE "/data/leon/recovery-phase"
#define TRIGGER_FILE "/run/leon/recovery-trigger"
#define TRIGGER_RESULT "/run/leon/recovery-trigger.result"
#define REPORT_FILE "/run/leon/recovery-tests.txt"
#define CHECK(c) do { if (!(c)) { \
    fprintf(report, "FAIL recovery:%d %s reply=%s errno=%d\n", __LINE__, #c, probe_reply(), errno); \
    fflush(report); return -1; } } while (0)
#define RPC(s, r) (!probe_rpc(s, r, 0) && probe_has("\"ok\":true"))

static FILE *report;

static int write_text(const char *path, const char *text, mode_t mode) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, mode);
    if (fd < 0) return -1;
    size_t size = strlen(text), used = 0;
    while (used < size) {
        ssize_t n = write(fd, text + used, size - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); return -1; }
        used += (size_t)n;
    }
    int bad = fsync(fd);
    close(fd);
    return bad ? -1 : 0;
}

static int stage(void) {
    int fd = open(STATE_FILE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return 0;
    char text[32] = {0};
    ssize_t n = read(fd, text, sizeof(text) - 1);
    close(fd);
    return n == 2 && text[0] >= '0' && text[0] <= '3' && text[1] == '\n' ? text[0] - '0' : -1;
}

static int save_stage(int value) {
    char text[8];
    snprintf(text, sizeof(text), "%d\n", value);
    char next[sizeof(STATE_FILE) + 6];
    snprintf(next, sizeof(next), "%s.next", STATE_FILE);
    if (write_text(next, text, 0600) || rename(next, STATE_FILE)) return -1;
    int dir = open("/data/leon", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int bad = dir < 0 || fsync(dir);
    if (dir >= 0) close(dir);
    return bad ? -1 : 0;
}

static int service_state(const char *service, const char *expected) {
    char command[128];
    snprintf(command, sizeof(command), "leonctl status %s 2>/dev/null", service);
    for (int attempt = 0; attempt < 100; ++attempt) {
        FILE *pipe = popen(command, "r");
        if (pipe) {
            char text[512] = {0};
            size_t used = fread(text, 1, sizeof(text) - 1, pipe);
            int status = pclose(pipe);
            if (!status && used && strstr(text, expected)) return 0;
        }
        usleep(100000);
    }
    return -1;
}

static int initialize(void) {
    CHECK(RPC("root", "{\"op\":\"enable\",\"password\":\"" ROOT_PASSWORD "\"}"));
    CHECK(RPC("apps", "{\"op\":\"install\",\"file\":\"" FIXTURE_PACKAGE "\"}"));
    CHECK(!mkdir("/data/leon/developer", 0700) || errno == EEXIST);
    CHECK(!mkdir("/data/leon/developer/services", 0700) || errno == EEXIST);
    CHECK(!write_text("/data/leon/developer/services/" OVERRIDDEN_SERVICE ".json",
        "{\"autostart\":false,\"restart\":\"always\",\"restart_limit\":10}", 0600));
    CHECK(!system("leonctl reload " OVERRIDDEN_SERVICE " >/dev/null && leonctl stop "
                  OVERRIDDEN_SERVICE " >/dev/null"));
    CHECK(!service_state(OVERRIDDEN_SERVICE, "stopped"));
    CHECK(!write_text("/data/home/leon/recovery-witness", "LeonMobile recovery preserves user data\n", 0600));
    CHECK(!chown("/data/home/leon/recovery-witness", 1000, 1000));
    CHECK(!chmod("/data/home/leon/recovery-witness", 0600));
    CHECK(!save_stage(0));
    fputs("RECOVERY_TESTS_WAITING phase=device-pin\n", report);
    return 0;
}

static int safe_boot(void) {
    CHECK(!service_state(OVERRIDDEN_SERVICE, "running"));
    FILE *mounts = setmntent("/proc/mounts", "r");
    struct mntent *entry;
    unsigned runtime_mounts = 0;
    CHECK(mounts);
    while ((entry = getmntent(mounts)))
        if (!strcmp(entry->mnt_dir, "/run") && !strcmp(entry->mnt_type, "tmpfs")) ++runtime_mounts;
    endmntent(mounts);
    CHECK(runtime_mounts == 1);
    FILE *status = fopen("/run/leon-boot/status", "r");
    char text[512] = {0};
    CHECK(status && fgets(text, sizeof(text), status));
    fclose(status);
    CHECK(strstr(text, "mode=safe\n"));
    CHECK(!probe_rpc("apps", "{\"op\":\"list\"}", 1000) &&
          probe_has("\"ok\":true") && !probe_has("org.example.hello"));
    CHECK(!probe_rpc("apps", "{\"op\":\"launch\",\"id\":\"org.example.hello\"}", 1000) &&
          probe_has("\"ok\":false") && probe_has("safe mode"));
    CHECK(RPC("root", "{\"op\":\"status\"}") && probe_has("\"enabled\":true") &&
          probe_has("\"password_set\":true"));
    CHECK(RPC("auth", "{\"op\":\"status\"}") && probe_has("\"configured\":true") &&
          probe_has("\"locked\":true") && probe_has("\"kind\":\"pin\""));
    CHECK(!save_stage(2));
    fputs("RECOVERY_TESTS_PASSED phase=safe\n", report);
    return 0;
}

static int normal_boot(void) {
    CHECK(!service_state(OVERRIDDEN_SERVICE, "stopped"));
    CHECK(RPC("apps", "{\"op\":\"list\"}") && probe_has("org.example.hello"));
    CHECK(RPC("root", "{\"op\":\"status\"}") && probe_has("\"enabled\":true") &&
          probe_has("\"password_set\":true"));
    CHECK(RPC("auth", "{\"op\":\"status\"}") && probe_has("\"configured\":true") &&
          probe_has("\"locked\":true") && probe_has("\"kind\":\"pin\""));
    for (int attempt = 0; attempt < 150; ++attempt) {
        if (!access("/run/leon/wayland/wayland-0", F_OK) && !system("pidof leon-shell >/dev/null 2>&1")) break;
        usleep(100000);
        CHECK(attempt != 149);
    }
    CHECK(RPC("apps", "{\"op\":\"launch\",\"id\":\"org.example.hello\"}"));
    struct stat home;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (!stat("/data/leon/data/org.example.hello", &home) && S_ISDIR(home.st_mode)) break;
        usleep(100000);
        CHECK(attempt != 99);
    }
    int fd = open("/data/leon/data/org.example.hello/recovery-witness",
                  O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    CHECK(fd >= 0 && !fchown(fd, home.st_uid, home.st_gid));
    const char witness[] = "installed app data survives system restore\n";
    CHECK(write(fd, witness, sizeof(witness) - 1) == (ssize_t)sizeof(witness) - 1 && !fsync(fd));
    close(fd);
    CHECK(!save_stage(3));
    fputs("RECOVERY_TESTS_PASSED phase=normal\n", report);
    return 0;
}

static int persist_next(unsigned mode) {
    struct leon_boot_store store;
    struct leon_boot_state state;
    if (leon_boot_store_open(&store, "/data/leon/boot")) return -1;
    int bad = leon_boot_state_load(&store, &state);
    if (!bad) {
        state.next = mode;
        state.requested = 1;
        bad = leon_boot_state_save(&store, &state);
    }
    leon_boot_store_close(&store);
    return bad;
}

static int damage_system(void) {
    int root = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (root < 0 || mount(NULL, "/", NULL, MS_REMOUNT, "rw")) {
        if (root >= 0) close(root);
        return -1;
    }
    int bad = unlink("/usr/bin/leon-shell") || fsync(root) || syncfs(root);
    int remounted = !mount(NULL, "/", NULL, MS_REMOUNT | MS_RDONLY, NULL);
    int remount_error = errno;
    close(root);
    /* A live service may keep the filesystem busy until PID 1 stops it. */
    if ((!remounted && remount_error != EBUSY) || bad) return -1;
    return access("/usr/bin/leon-shell", F_OK) == 0 ? -1 : 0;
}

static void handle_trigger(void) {
    char mode[32] = {0};
    int fd = open(TRIGGER_FILE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return;
    ssize_t n = read(fd, mode, sizeof(mode) - 1);
    close(fd);
    if (n <= 0) return;
    if (strcmp(mode, "safe\n") && strcmp(mode, "normal\n") && strcmp(mode, "damage\n")) return;
    const char *kind = !strcmp(mode, "safe\n") ? "safe" : !strcmp(mode, "normal\n") ? "normal" : "damage";
    int result = persist_next(!strcmp(kind, "safe") ? LEON_BOOT_SAFE : LEON_BOOT_NORMAL);
    if (!result && !strcmp(kind, "damage")) result = damage_system();
    if (result) {
        unlink(TRIGGER_FILE);
        char failure[96]; snprintf(failure, sizeof(failure), "failed %s errno=%d\n", kind, errno);
        (void)write_text(TRIGGER_RESULT, failure, 0644);
        return;
    }
    unlink(TRIGGER_FILE);
    char accepted[64]; snprintf(accepted, sizeof(accepted), "accepted %s\n", kind);
    if (write_text(TRIGGER_RESULT, accepted, 0644)) return;
    sync();
    sleep(2); /* Let the serial command that wrote the trigger finish. */
    kill(1, SIGUSR2);
    for (;;) pause();
}

static int watch(void) {
    if (write_text(TRIGGER_FILE, "", 0666) || chmod(TRIGGER_FILE, 0666)) return 1;
    unsigned status_ticks = 0;
    for (;;) {
        if (stage() == 0 && !(status_ticks++ % 10)) {
            if (RPC("auth", "{\"op\":\"status\"}")) {
                (void)write_text("/run/leon/recovery-auth-status", probe_reply(), 0644);
                if (probe_has("\"configured\":true") && probe_has("\"kind\":\"pin\"")) {
                    if (save_stage(1)) return 1;
                    int fd = open(REPORT_FILE, O_WRONLY | O_APPEND | O_CLOEXEC | O_NOFOLLOW);
                    if (fd < 0 || write(fd, "RECOVERY_TESTS_PASSED phase=setup\n", 34) != 34 || fsync(fd)) {
                        if (fd >= 0) close(fd);
                        return 1;
                    }
                    close(fd);
                }
            }
        }
        handle_trigger();
        usleep(100000);
    }
}

int main(int argc, char **argv) {
    (void)argv;
    if (geteuid() || argc != 2 || strcmp(argv[1], "--watch")) return 2;
    report = fopen(REPORT_FILE, "w");
    if (!report) return 2;
    setbuf(report, NULL);
    int current = stage(), result;
    if (current < 0 || current > 3) result = -1;
    else if (current == 0) result = initialize();
    else if (current == 1) result = safe_boot();
    else if (current == 2) result = normal_boot();
    else result = !service_state(OVERRIDDEN_SERVICE, "stopped") ? 0 : -1;
    if (result) {
        fputs("RECOVERY_TESTS_FAILED\n", report);
        fclose(report);
        return 1;
    }
    return watch();
}
