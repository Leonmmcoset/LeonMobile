#include "service.h"
#include "daemon.h"
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

struct network_state {
    pid_t dhcp;
    int wanted, failures;
    long long next, generation, sample;
    uint64_t fingerprint;
    char interface[IFNAMSIZ], error[128];
};
static lm_json *status(struct lm_daemon *d);
static void stop_dhcp(struct network_state *s) {
    if (s->dhcp > 0) {
        kill(-s->dhcp, SIGTERM);
        for (int i = 0; i < 50; ++i) {
            if (waitpid(s->dhcp, NULL, WNOHANG) == s->dhcp) {
                s->dhcp = 0;
                return;
            }
            usleep(10000);
        }
        kill(-s->dhcp, SIGKILL);
        waitpid(s->dhcp, NULL, 0);
        s->dhcp = 0;
    }
}
static void tick(struct lm_daemon *d) {
    struct network_state *s = d->data;
    if (lm_now_ms() >= s->sample) {
        s->sample = lm_now_ms() + 1000;
        lm_json *snapshot = status(d);
        lm_free(snapshot);
    }
    if (s->dhcp) {
        int status;
        pid_t p = waitpid(s->dhcp, &status, WNOHANG);
        if (p == s->dhcp) {
            s->dhcp = 0;
            s->next = lm_now_ms() + 1000 * (1 << (s->failures < 5 ? s->failures : 5));
            ++s->failures;
            snprintf(s->error, sizeof(s->error), "DHCP client exited (%d)",
                     WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
            ++s->generation;
        }
    }
    if (!s->wanted || s->dhcp || d->options.test || lm_now_ms() < s->next)
        return;
    pid_t parent = getpid();
    pid_t pid = fork();
    if (!pid) {
        if (lm_child_setup(parent))
            _exit(126);
        setsid();
        execl("/sbin/udhcpc", "udhcpc", "-f", "-i", s->interface, "-s", "/usr/libexec/leon/udhcpc",
              "-t", "5", "-T", "2", "-A", "10", NULL);
        _exit(127);
    }
    if (pid > 0) {
        s->dhcp = pid;
        ++s->generation;
    } else {
        s->next = lm_now_ms() + 2000;
        snprintf(s->error, sizeof(s->error), "cannot fork DHCP client");
    }
}
static lm_json *status(struct lm_daemon *d) {
    struct network_state *s = d->data;
    lm_json *out = lm_success(), *interfaces = lm_array();
    struct ifaddrs *addresses = NULL;
    if (!getifaddrs(&addresses)) {
        for (struct ifaddrs *a = addresses; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET)
                continue;
            char address[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &((struct sockaddr_in *)a->ifa_addr)->sin_addr, address,
                      sizeof(address));
            lm_json *item = lm_object();
            lm_set_string(item, "name", a->ifa_name);
            lm_set_string(item, "ipv4", address);
            lm_set_boolean(item, "up", a->ifa_flags & IFF_UP);
            lm_set_boolean(item, "running", a->ifa_flags & IFF_RUNNING);
            lm_append(interfaces, item);
        }
        freeifaddrs(addresses);
    }
    lm_set(out, "interfaces", interfaces);
    lm_set_string(out, "managed_interface", s->interface);
    lm_set_boolean(out, "enabled", s->wanted);
    lm_set_integer(out, "dhcp_pid", s->dhcp);

    lm_set_string(out, "error", s->error);
    char path[PATH_MAX], text[8192] = "";
    snprintf(path, sizeof(path), "%s/net/route", d->options.proc);
    lm_read_text(path, text, sizeof(text));
    lm_set_string(out, "routes", text);
    text[0] = 0;
    lm_read_text(d->options.test ? d->options.state : "/run/resolv.conf", text, sizeof(text));
    lm_set_string(out, "resolver", text);
    if (strstr(text, "nameserver ") && lm_size(lm_get(out, "interfaces")) > 1) {
        s->error[0] = 0;
        lm_set_string(out, "error", "");
    }
    char *encoded = lm_dump(out);
    uint64_t hash = 1469598103934665603ULL;
    if (encoded)
        for (const unsigned char *p = (unsigned char *)encoded; *p; ++p) {
            hash ^= *p;
            hash *= 1099511628211ULL;
        }
    free(encoded);
    int changed = hash != s->fingerprint;
    if (changed) {
        s->fingerprint = hash;
        ++s->generation;
    }
    lm_set_integer(out, "generation", s->generation);
    if (changed && !d->options.test) {
        if (lm_write_json("/run/leon/network-state.json", out) ||
            chmod("/run/leon/network-state.json", 0644))
            fprintf(stderr, "netd: cannot publish state\n");
    }
    return out;
}
static lm_json *handle(struct lm_daemon *d, uid_t uid, const lm_json *r) {
    struct network_state *s = d->data;
    const char *op = lm_string(lm_get(r, "op"), "");
    if (!strcmp(op, "status") || !strcmp(op, "watch")) {
        long long since = lm_integer(lm_get(r, "since"), -1);
        if (!strcmp(op, "watch") && since < 0)
            return lm_failure("since must be a nonnegative generation");
        lm_json *out = status(d);
        lm_set_boolean(out, "changed", since != s->generation);
        return out;
    }
    if (!lm_trusted(uid))
        return lm_failure("permission denied");
    if (strcmp(op, "enable") && strcmp(op, "disable") && strcmp(op, "renew"))
        return lm_failure("unknown operation");
    if (d->options.test)
        return lm_failure("network mutations disabled for test roots");
    if (!strcmp(op, "disable")) {
        s->wanted = 0;
        stop_dhcp(s);
        char *args[] = {"/sbin/ip", "addr", "flush", "dev", s->interface, NULL};
        if (lm_child(args, NULL, 1000))
            return lm_failure("cannot flush managed addresses");
        char *down[] = {"/sbin/ip", "link", "set", s->interface, "down", NULL};
        if (lm_child(down, NULL, 1000))
            return lm_failure("cannot disable interface");
        if (lm_write_text("/run/resolv.conf", "") || chmod("/run/resolv.conf", 0644))
            return lm_failure("cannot clear resolver");
    } else if (!strcmp(op, "renew") && s->dhcp)
        kill(s->dhcp, SIGUSR1);
    else {
        s->wanted = 1;
        s->failures = 0;
        s->next = 0;
        tick(d);
    }
    ++s->generation;
    return status(d);
}
int netd_run(int argc, char **argv) {
    struct network_state state = {.wanted = 1, .interface = "eth0"};
    struct lm_daemon d = {.options = {"/run/leon/network.sock", "/run/resolv.conf", "/data/leon",
                                      "/usr/share/leon/apps", "/proc", "/sys", 0},
                          .data = &state,
                          .handle = handle,
                          .tick = tick};
    if (lm_options(argc, argv, &d.options))
        return 2;
    int result = lm_serve(&d);
    stop_dhcp(&state);
    return result;
}
