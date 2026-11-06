#include "state.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void) {
    if (geteuid()) return 0;
    char path[] = "/tmp/leon-boot-XXXXXX";
    assert(mkdtemp(path));
    struct leon_boot_store store, second;
    struct leon_boot_state s = {0}, saved;
    const char *id = "11111111-1111-1111-1111-111111111111";
    assert(!leon_boot_store_open(&store, path));
    assert(leon_boot_store_open(&second, path));
    assert(leon_boot_state_load(&store, &s) == 1);
    for (int i = 0; i < 4; ++i) {
        assert(!leon_boot_begin(&s, id, 0));
        assert(s.failures == (unsigned)i);
        assert(s.mode == (i == 3 ? LEON_BOOT_RECOVERY : LEON_BOOT_NORMAL));
        assert(!leon_boot_state_save(&store, &s));
        assert(!leon_boot_state_load(&store, &saved));
        assert(!memcmp(&s, &saved, sizeof(s)));
    }
    assert(leon_boot_success(&s, id));
    s.next = LEON_BOOT_SAFE;
    assert(!leon_boot_begin(&s, id, 0) && s.mode == LEON_BOOT_SAFE);
    assert(leon_boot_success(&s, "22222222-2222-2222-2222-222222222222"));
    assert(!leon_boot_success(&s, id) && !s.failures && !s.pending);
    s.next = LEON_BOOT_NORMAL; s.requested = 1;
    assert(!leon_boot_begin(&s, id, LEON_BOOT_RECOVERY) && s.mode == LEON_BOOT_NORMAL);
    assert(!s.requested && !leon_boot_success(&s, id));
    assert(!leon_boot_begin(&s, id, LEON_BOOT_RECOVERY) && s.mode == LEON_BOOT_RECOVERY);
    assert(!leon_boot_state_save(&store, &s));
    assert(!symlinkat("/etc/passwd", store.directory, "state.next"));
    assert(!leon_boot_state_save(&store, &s));
    assert(!linkat(store.directory, "state", store.directory, "linked", 0));
    assert(leon_boot_state_load(&store, &saved) < 0);
    assert(!unlinkat(store.directory, "linked", 0));
    assert(!fchmodat(store.directory, "state", 0644, 0));
    assert(leon_boot_state_load(&store, &saved) < 0);
    assert(!fchmodat(store.directory, "state", 0600, 0));
    int fd = openat(store.directory, "state", O_WRONLY | O_TRUNC);
    assert(fd >= 0 && write(fd, "broken", 6) == 6); close(fd);
    assert(leon_boot_state_load(&store, &saved) < 0);
    assert(!unlinkat(store.directory, "state", 0));
    assert(!symlinkat("/etc/passwd", store.directory, "state"));
    assert(leon_boot_state_load(&store, &saved) < 0);
    assert(!unlinkat(store.directory, "state", 0));
    s.reason = LEON_BOOT_REPAIRING;
    assert(!leon_boot_begin(&s, id, LEON_BOOT_NORMAL) && s.mode == LEON_BOOT_RECOVERY);
    leon_boot_store_close(&store);
    char lock[256]; snprintf(lock, sizeof(lock), "%s/lock", path); assert(!unlink(lock));
    assert(!rmdir(path));
    puts("boot state: durable transitions, recovery threshold, safe boot, identity, corruption, hardlinks and symlinks passed");
    return 0;
}
