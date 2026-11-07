#define _GNU_SOURCE
#include "system_probe.h"
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// Test-only setuid observation/seeding helper, injected into disposable images.
// No operation confirms an installation; the graphical user does that.
static int copy_package(const char *name, const char *directory, uid_t owner) {
    char source[256], target[256], buffer[16384];
    snprintf(source, sizeof(source), "/usr/share/installer-test/%s", name);
    snprintf(target, sizeof(target), "%s/%s", directory, name);
    int input = open(source, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    int output = open(target, O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (input < 0 || output < 0) return -1;
    ssize_t n;
    while ((n = read(input, buffer, sizeof(buffer))) > 0)
        if (write(output, buffer, (size_t)n) != n) return -1;
    int result = n < 0 || fchown(output, owner, owner) || fsync(output);
    close(input); close(output);
    return result ? -1 : 0;
}
int main(int argc, char **argv) {
    if (geteuid() || getuid() != 1000 || argc != 2) return 2;
    if (!strcmp(argv[1], "status")) {
        if (probe_rpc("apps", "{\"op\":\"package_status\"}", 11008)) return 1;
        puts(probe_reply()); return 0;
    }
    if (!strcmp(argv[1], "discard")) {
        if (probe_rpc("apps", "{\"op\":\"package_cancel\"}", 11008) || !probe_has("\"ok\":true")) return 1;
        puts(probe_reply()); return 0;
    }
    if (!strcmp(argv[1], "permissions")) {
        if (probe_rpc("portal", "{\"op\":\"app_permissions\",\"app\":\"org.example.hello\"}", 0)) return 1;
        puts(probe_reply()); return 0;
    }
    if (!strcmp(argv[1], "diagnose")) {
        if (probe_rpc("documents", "{\"op\":\"offer\",\"file\":\"01-install.lmpkg\"}", 11003)) return 1;
        puts(probe_reply());
        const char *value = strstr(probe_reply(), "\"token\":\"");
        if (value) {
            char request[256], token[33];
            memcpy(token, value + 9, 32); token[32] = 0;
            snprintf(request, sizeof(request), "{\"op\":\"delegate\",\"token\":\"%s\",\"from_uid\":11003,\"to_uid\":11008}", token);
            struct timespec before, after;
            clock_gettime(CLOCK_MONOTONIC, &before);
            if (probe_rpc("documents", request, 0)) return 1;
            clock_gettime(CLOCK_MONOTONIC, &after);
            printf("delegation took %ld ms: %s\n", (after.tv_sec - before.tv_sec) * 1000 + (after.tv_nsec - before.tv_nsec) / 1000000, probe_reply());
        }
        if (probe_rpc("intents", "{\"op\":\"resolve\",\"action\":\"open\",\"mime\":\"application/vnd.leonmobile.package\",\"document\":\"ffffffffffffffffffffffffffffffff\"}", 11003)) return 1;
        puts(probe_reply()); return 0;
    }
    if (!strcmp(argv[1], "denials")) {
        const uid_t peers[] = {1000, 11000, 11003, 12000};
        for (size_t i = 0; i < sizeof(peers) / sizeof(peers[0]); ++i)
            if (probe_rpc("apps", "{\"op\":\"package_commit\",\"token\":\"forged\"}", peers[i]) ||
                !probe_has("\"ok\":false")) return 1;
        puts("INSTALLER_PEER_DENIALS_PASSED"); return 0;
    }
    if (!strcmp(argv[1], "files")) {
        struct dirent **entries;
        int count = scandir("/data/leon/data/org.leon.files", &entries, NULL, alphasort);
        if (count < 0) return 1;
        for (int i = 0; i < count; ++i) {
            if (strcmp(entries[i]->d_name, ".") && strcmp(entries[i]->d_name, "..")) puts(entries[i]->d_name);
            free(entries[i]);
        }
        free(entries); return 0;
    }
    if (!strcmp(argv[1], "seed")) {
        const char *files = "/data/leon/data/org.leon.files";
        if ((mkdir(files, 0700) && errno != EEXIST) || chown(files, 11003, 11003) ||
            chmod(files, 0700)) return 1;
        const char *names[] = {"01-install.lmpkg", "02-update.LMPKG", "03-invalid.lmpkg"};
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (copy_package(names[i], files, 11003)) return 1;
        const char *shared = "/data/leon/shared";
        if (mkdir(shared, 0755) && errno != EEXIST) return 1;
        if (copy_package("04-picker.lmpkg", shared, 0)) return 1;
        const char *home = "/data/leon/data/org.example.hello";
        if ((mkdir(home, 0700) && errno != EEXIST) || chown(home, 12000, 12000)) return 1;
        int fd = open("/data/leon/data/org.example.hello/installer-witness", O_CREAT | O_EXCL | O_WRONLY, 0600);
        const char *witness = "graphical upgrade preserves private data\n";
        if (fd < 0 || write(fd, witness, strlen(witness)) != (ssize_t)strlen(witness) ||
            fchown(fd, 12000, 12000) || fsync(fd)) return 1;
        close(fd);
        puts("INSTALLER_FIXTURES_READY"); return 0;
    }
    return 2;
}
