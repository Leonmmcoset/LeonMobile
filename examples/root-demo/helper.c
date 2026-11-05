#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (geteuid() || argc != 2 || strcmp(argv[1], "inspect"))
        return 2;
    struct utsname kernel;
    if (uname(&kernel))
        return 1;
    printf("Root helper UID: %u\nLinux: %s\nArchitecture: %s\nSystem: LeonMobile 1.0.0\nRead-only "
           "diagnostic completed.\n",
           getuid(), kernel.release, kernel.machine);
    return 0;
}
