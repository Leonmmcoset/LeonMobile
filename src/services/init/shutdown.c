#include "init.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <unistd.h>
_Noreturn void leon_shutdown(pid_t manager, int action) {
    int how = (action == SIGINT || action == SIGUSR2) ? RB_AUTOBOOT : RB_POWER_OFF;
    if (manager > 0)
        kill(manager, SIGTERM);
    for (int i = 0; i < 350; ++i) {
        int status;
        if (waitpid(manager, &status, WNOHANG) == manager)
            break;
        usleep(100000);
    }
    kill(-1, SIGTERM);
    usleep(300000);
    kill(-1, SIGKILL);
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
    sync();
    FILE *mounts = fopen("/proc/mounts", "r");
    if (mounts) {
        char line[4096], target[1024];
        while (fgets(line, sizeof(line), mounts))
            if (sscanf(line, "%*s %1023s", target) == 1 && !strncmp(target, "/media/", 7))
                umount2(target, 0);
        fclose(mounts);
    }
    umount2("/media", 0);
    umount2("/var", 0);
    umount2("/data", 0);
    mount(NULL, "/", NULL, MS_REMOUNT | MS_RDONLY, NULL);
    reboot(how);
    for (;;)
        pause();
}
