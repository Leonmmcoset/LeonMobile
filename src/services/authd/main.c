#include "authd.h"
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc, char **argv) {
    struct auth_daemon d = {
        .directory = "/data/leon/authd", .socket = LEON_AUTH_SOCKET, .locked = 1, .watch = -1};
    const char *boot = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--test")) {
            d.test = 1;
            continue;
        }
        const char **field = !strcmp(argv[i], "--state-dir") ? &d.directory
                             : !strcmp(argv[i], "--socket")  ? &d.socket
                             : !strcmp(argv[i], "--boot-id") ? &boot
                                                             : NULL;
        if (!field || i + 1 == argc)
            return 2;
        *field = argv[++i];
    }
    if (geteuid() || (boot && !d.test))
        return 2;
    if (boot)
        snprintf(d.boot, sizeof(d.boot), "%s", boot);
    else if (lm_read_text("/proc/sys/kernel/random/boot_id", d.boot, sizeof(d.boot)))
        return 1;
    d.boot[strcspn(d.boot, "\r\n")] = 0;
    if (!*d.boot)
        return 1;
    umask(077);
    struct rlimit core = {0, 0};
    setrlimit(RLIMIT_CORE, &core);
    prctl(PR_SET_DUMPABLE, 0);
    if (auth_policy_open(&d))
        return 1;
    d.valid = !auth_policy_load(&d);
    auth_set_locked(&d);
    int result = auth_server(&d);
    close(d.singleton);
    credential_clear(&d, sizeof(d));
    return result;
}
