#include "service.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
lm_json *network_status(struct lm_daemon *d) {
    struct network_state *s = d->data;
    lm_json *out = lm_success(), *interfaces = lm_array();
    int link = 0, addressed = 0, route = 0;
    struct ifaddrs *addresses = NULL;
    if (!getifaddrs(&addresses)) {
        for (struct ifaddrs *a = addresses; a; a = a->ifa_next) {
            if (!a->ifa_addr)
                continue;
            int managed = !strcmp(a->ifa_name, s->interface);
            if (managed && (a->ifa_flags & IFF_UP) && (a->ifa_flags & IFF_RUNNING))
                link = 1;
            if (a->ifa_addr->sa_family != AF_INET)
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
            if (managed && strcmp(address, "0.0.0.0"))
                addressed = 1;
        }
        freeifaddrs(addresses);
    }
    lm_set(out, "interfaces", interfaces);
    lm_set_string(out, "managed_interface", s->interface);
    lm_set_boolean(out, "enabled", s->wanted);
    lm_set_integer(out, "dhcp_pid", s->dhcp);
    lm_set_boolean(out, "configuration_pending", s->apply_needed || s->apply);
    lm_set(out, "configuration", lm_copy(s->policy));
    char path[PATH_MAX], text[8192] = "";
    snprintf(path, sizeof(path), "%s/net/route", d->options.proc);
    lm_read_text(path, text, sizeof(text));
    lm_set_string(out, "routes", text);
    for (char *line = text; line && *line;) {
        char name[IFNAMSIZ];
        unsigned destination, gateway, flags;
        if (sscanf(line, "%15s %x %x %x", name, &destination, &gateway, &flags) == 4 &&
            !strcmp(name, s->interface) && destination == 0 && (flags & 1))
            route = 1;
        line = strchr(line, '\n');
        if (line)
            ++line;
    }
    text[0] = 0;
    lm_read_text(d->options.test ? d->options.state : "/run/leon-system/resolv.conf", text, sizeof(text));
    lm_set_string(out, "resolver", text);
    lm_set_boolean(out, "carrier", link);
    lm_set_boolean(out, "connected",
                   s->wanted && !s->apply_needed && !s->apply && link && addressed && route &&
                       strstr(text, "nameserver "));
    const char *state = !s->wanted                     ? "disabled"
                        : s->apply_needed || s->apply  ? "configuring"
                        : !link                        ? "disconnected"
                        : !addressed                   ? "configuring"
                        : !route                       ? "local"
                        : !strstr(text, "nameserver ") ? "degraded"
                                                       : "connected";
    lm_set_string(out, "connection_state", state);
    lm_set_string(out, "internet", "unverified");
    if (!strcmp(state, "connected") && !s->apply && !s->apply_needed) {
        s->failures = 0;
        s->error[0] = 0;
    }
    lm_set_string(out, "error", s->error);
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
    if (changed && !d->options.test &&
        (lm_write_json("/run/leon/network-state.json", out) ||
         chmod("/run/leon/network-state.json", 0644)))
        fputs("netd: cannot publish network state\n", stderr);
    return out;
}
