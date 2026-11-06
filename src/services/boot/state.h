#pragma once
#include <stddef.h>
enum leon_boot_mode { LEON_BOOT_NORMAL, LEON_BOOT_SAFE, LEON_BOOT_RECOVERY };
enum leon_boot_reason {
    LEON_BOOT_OK, LEON_BOOT_UNFINISHED, LEON_BOOT_SYSTEM_MOUNT, LEON_BOOT_MISSING,
    LEON_BOOT_TIMEOUT, LEON_BOOT_CORRUPT, LEON_BOOT_DATA, LEON_BOOT_REQUESTED,
    LEON_BOOT_REPAIRING, LEON_BOOT_REPAIRED, LEON_BOOT_VERIFY
};
struct leon_boot_state {
    unsigned attempts, failures, pending, mode, next, reason, requested;
    char boot_id[37];
};
struct leon_boot_store { int directory, lock; };
const char *leon_boot_mode_name(unsigned mode);
const char *leon_boot_reason_name(unsigned reason);
int leon_boot_store_open(struct leon_boot_store *store, const char *path);
void leon_boot_store_close(struct leon_boot_store *store);
/* 1 = absent, 0 = valid, -1 = invalid or I/O error; caller holds the lock. */
int leon_boot_state_load(struct leon_boot_store *store, struct leon_boot_state *state);
int leon_boot_state_save(struct leon_boot_store *store, const struct leon_boot_state *state);
int leon_boot_begin(struct leon_boot_state *state, const char *boot_id, unsigned requested);
int leon_boot_success(struct leon_boot_state *state, const char *boot_id);
int leon_boot_id(char id[37]);
int leon_boot_publish(const struct leon_boot_state *state, const char *directory);
int leon_boot_safe(void);
