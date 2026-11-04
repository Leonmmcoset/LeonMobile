#include "auth.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
static int executable_matches(pid_t pid, const char *executable) {
    char path[64];
    struct stat actual, installed;
    snprintf(path, sizeof(path), "/proc/%d/exe", pid);
    return !stat(path, &actual) && !stat(executable, &installed) &&
           actual.st_dev == installed.st_dev && actual.st_ino == installed.st_ino;
}
int auth_peer(pid_t pid, uid_t uid, uid_t expected, const char *executable, const char *parent) {
    if (uid != expected || !executable_matches(pid, executable))
        return 0;
    if (!parent)
        return 1;
    char path[64], text[4096];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *file = fopen(path, "re");
    if (!file)
        return 0;
    int ok = fgets(text, sizeof(text), file) != NULL;
    fclose(file);
    char *end = ok ? strrchr(text, ')') : NULL, state;
    int ppid = 0;
    return end && sscanf(end + 1, " %c %d", &state, &ppid) == 2 && executable_matches(ppid, parent);
}
