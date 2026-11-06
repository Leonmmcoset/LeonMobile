#pragma once
#include "state.h"
#include <stdint.h>
struct recovery {
    struct leon_boot_state state;
    unsigned reason;
    int data_available, console[2];
    char message[160];
};
int recovery_console_open(struct recovery *r);
void recovery_draw(struct recovery *r, const char *page);
int recovery_line(struct recovery *r, char *text, unsigned capacity, int secret);
int recovery_restore(struct recovery *r);
int recovery_check(struct recovery *r);
int recovery_report(struct recovery *r, int export);
int recovery_authenticate(struct recovery *r);
int recovery_verify(int fd, uint64_t size, const char *hash);
int recovery_hash(int fd, uint64_t size, char hash[65]);
void recovery_reboot(struct recovery *r, unsigned mode, int poweroff);
int recovery_save(struct recovery *r);
