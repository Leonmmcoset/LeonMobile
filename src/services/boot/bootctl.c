#include "state.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv) {
    const char *path = "/data/leon/boot";
    int i = 1, custom = 0;
    if (argc > 3 && !strcmp(argv[1], "--state-dir")) { path = argv[2]; i = 3; custom = 1; }
    if (geteuid() || argc <= i) {
        fputs("usage (root): leon-bootctl [--state-dir DIR] status | next normal|safe|recovery | reboot normal|safe|recovery\n", stderr);
        return 2;
    }
    struct leon_boot_store store;
    struct leon_boot_state state;
    if (leon_boot_store_open(&store, path)) { perror("boot state"); return 1; }
    int bad = leon_boot_state_load(&store, &state);
    if (!bad && !strcmp(argv[i], "status") && argc == i + 1) {
        printf("mode=%s\nnext=%s\nhealthy=%s\nattempts=%u\nfailures=%u\nreason=%s\nboot_id=%s\n",
            leon_boot_mode_name(state.mode), leon_boot_mode_name(state.next),
            state.pending || state.mode == LEON_BOOT_RECOVERY ? "false" : "true",
            state.attempts, state.failures, leon_boot_reason_name(state.reason), state.boot_id);
    } else if (!bad && argc == i + 2 && (!strcmp(argv[i], "next") || !strcmp(argv[i], "reboot"))) {
        unsigned mode;
        for (mode = 0; mode <= LEON_BOOT_RECOVERY; ++mode)
            if (!strcmp(argv[i + 1], leon_boot_mode_name(mode))) break;
        if (mode > LEON_BOOT_RECOVERY || (custom && !strcmp(argv[i], "reboot"))) bad = -1;
        else { state.next = mode; state.requested = 1; bad = leon_boot_state_save(&store, &state); }
    } else bad = -1;
    leon_boot_store_close(&store);
    if (bad) { fputs("Boot operation failed; state was not reset.\n", stderr); return 1; }
    if (!strcmp(argv[i], "reboot")) { sync(); if (kill(1, SIGUSR2)) return 1; }
    return 0;
}
