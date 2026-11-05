#include "notificationd.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void usage(FILE *stream) {
    fputs("usage: notificationd [--root DIR] [--builtin DIR] [--socket PATH] [--auth-socket PATH] [--audio-socket PATH]\n", stream);
}

int main(int argc, char **argv) {
    struct notification_options options = {"/data/leon", "/usr/share/leon/apps",
                                           "/run/leon/notifications.sock", "/run/leon-auth/control.sock", "/run/leon/audio.sock", 0};
    int auth_override=0,audio_override=0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help")) {
            usage(stdout);
            return 0;
        }
        if (i + 1 == argc) {
            usage(stderr);
            return 2;
        }
        if (!strcmp(argv[i], "--root")) {
            options.development=1;
            options.root = argv[++i];
        } else if (!strcmp(argv[i],"--auth-socket")) {options.auth_socket=argv[++i];auth_override=1;}
        else if (!strcmp(argv[i],"--audio-socket")) {options.audio_socket=argv[++i];audio_override=1;}
        else if (!strcmp(argv[i], "--builtin"))
            options.builtin = argv[++i];
        else if (!strcmp(argv[i], "--socket"))
            options.socket_path = argv[++i];
        else {
            usage(stderr);
            return 2;
        }
    }
    if (geteuid()) {
        fputs("notificationd requires root\n", stderr);
        return 1;
    }
    if(options.development) {
        if(!auth_override) options.auth_socket="";
        if(!audio_override) options.audio_socket="";
    }
    return notification_serve(&options);
}
