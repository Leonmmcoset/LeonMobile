#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int parse(const char *text, struct service *s) {
    char path[] = "/tmp/leon-service-XXXXXX", error[256];
    int fd = mkstemp(path); assert(fd >= 0);
    assert(write(fd, text, strlen(text)) == (ssize_t)strlen(text)); close(fd);
    int rc = service_load(path, s, error, sizeof(error)); unlink(path); return rc;
}
int main(void) {
    struct service s[2]; char error[256]; int order[2];
    assert(!parse("name = \"ui\"\ncommand = [\"/bin/ui\", \"a b\"]\nafter = [\"seat\"]\nuid = 1000\nrestart = \"on-failure\"\n", &s[0]));
    assert(!strcmp(s[0].argv[1], "a b") && s[0].uid == 1000);
    assert(!parse("name = \"seat\"\ncommand = [\"/bin/seatd\"]\n", &s[1]));
    assert(!service_order(s, 2, order, error, sizeof(error)) && order[0] == 1);
    s[1].after[0] = strdup("ui"); assert(service_order(s, 2, order, error, sizeof(error)) < 0);
    service_free(&s[0]); service_free(&s[1]);
    assert(parse("name = \"bad\"\ncommand = [\"relative\"]\n", &s[0]) < 0);
    assert(parse("name = \"bad\"\nname = \"dup\"\ncommand = [\"/bin/a\"]\n", &s[0]) < 0);
    assert(parse("name = \"bad\"\ncommand = [\"/bin/a\"]\nuid = -1\n", &s[0]) < 0);
    puts("service configuration and dependency tests passed");
}
