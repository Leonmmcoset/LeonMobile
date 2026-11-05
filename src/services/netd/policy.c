#include "service.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
lm_json *network_defaults(void) {
    const char *text = "{\"enabled\":true,\"method\":\"dhcp\",\"address\":\"\",\"prefix\":24,"
                       "\"gateway\":\"\",\"dns\":[]}";
    return lm_parse(text, strlen(text));
}
int network_validate(const lm_json *p) {
    if (!lm_is_object(p) || lm_size(p) != 6 || lm_boolean(lm_get(p, "enabled"), -1) < 0)
        return 0;
    const char *method = lm_string(lm_get(p, "method"), "");
    if (strcmp(method, "dhcp") && strcmp(method, "static"))
        return 0;
    const char *address = lm_string(lm_get(p, "address"), NULL);
    const char *gateway = lm_string(lm_get(p, "gateway"), NULL);
    int64_t prefix = lm_integer(lm_get(p, "prefix"), -1);
    if (!address || !gateway || prefix < 1 || prefix > 32)
        return 0;
    struct in_addr a = {0}, g = {0};
    if ((*address && (inet_pton(AF_INET, address, &a) != 1 || !a.s_addr ||
                      (ntohl(a.s_addr) >> 24) == 127 || (ntohl(a.s_addr) >> 28) >= 14)) ||
        (*gateway && (inet_pton(AF_INET, gateway, &g) != 1 || !g.s_addr ||
                      (ntohl(g.s_addr) >> 24) == 127 || (ntohl(g.s_addr) >> 28) >= 14)))
        return 0;
    if (!strcmp(method, "static")) {
        if (!*address)
            return 0;
        uint32_t mask = UINT32_MAX << (32 - prefix), host = ntohl(a.s_addr) & ~mask;
        if ((prefix < 31 && (!host || host == ~mask)) ||
            (*gateway &&
             (a.s_addr == g.s_addr || (ntohl(a.s_addr) & mask) != (ntohl(g.s_addr) & mask))))
            return 0;
    }
    const lm_json *dns = lm_get(p, "dns");
    if (!lm_is_array(dns) || lm_size(dns) > 3)
        return 0;
    for (size_t i = 0; i < lm_size(dns); ++i) {
        const char *s = lm_string(lm_at(dns, i), "");
        struct in6_addr v6;
        if ((inet_pton(AF_INET, s, &a) != 1 || !a.s_addr || (ntohl(a.s_addr) >> 28) >= 14) &&
            (inet_pton(AF_INET6, s, &v6) != 1 || IN6_IS_ADDR_UNSPECIFIED(&v6) ||
             IN6_IS_ADDR_MULTICAST(&v6)))
            return 0;
    }
    return 1;
}
int network_load(struct lm_daemon *d) {
    struct network_state *s = d->data;
    if (snprintf(s->policy_path, sizeof(s->policy_path), "%s/system/network.json",
                 d->options.root) >= (int)sizeof(s->policy_path))
        return -1;
    s->policy = lm_read_json(s->policy_path);
    if (!s->policy) {
        struct stat st;
        if (!lstat(s->policy_path, &st) || errno != ENOENT)
            return -1;
        s->policy = network_defaults();
    }
    if (!network_validate(s->policy))
        return -1;
    s->wanted = lm_boolean(lm_get(s->policy, "enabled"), 1);
    s->apply_needed = !d->options.test;
    return 0;
}
