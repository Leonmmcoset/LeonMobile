#include "service.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
void network_stop(pid_t *pid) {
    if (*pid <= 0)
        return;
    kill(-*pid, SIGTERM);
    for (int i = 0; i < 30; ++i) {
        pid_t result = waitpid(*pid, NULL, WNOHANG);
        if (result == *pid || (result < 0 && errno == ECHILD)) {
            *pid = 0;
            return;
        }
        usleep(1000);
    }
    kill(-*pid, SIGKILL);
    while (waitpid(*pid, NULL, 0) < 0 && errno == EINTR) {
    }
    *pid = 0;
}
static void resolver(struct lm_daemon *d) {
    struct network_state *s = d->data;
    char text[512] = "", old[512] = "";
    if (s->wanted) {
        const lm_json *dns = lm_get(s->policy, "dns");
        if (lm_size(dns)) {
            size_t used = 0;
            for (size_t i = 0; i < lm_size(dns); ++i)
                used += (size_t)snprintf(text + used, sizeof(text) - used, "nameserver %s\n",
                                         lm_string(lm_at(dns, i), ""));
        } else if (!strcmp(lm_string(lm_get(s->policy, "method"), ""), "dhcp"))
            lm_read_text("/run/leon/dhcp-resolv.conf", text, sizeof(text));
    }
    if (lm_read_text("/run/resolv.conf", old, sizeof(old)) || strcmp(text, old))
        if (lm_write_text("/run/resolv.conf", text) || chmod("/run/resolv.conf", 0644))
            snprintf(s->error, sizeof(s->error), "cannot publish resolver");
}
void network_tick(struct lm_daemon *d) {
    struct network_state *s = d->data;
    long long now = lm_now_ms();
    if (!d->options.test && s->apply) {
        int status = 0;
        pid_t p = waitpid(s->apply, &status, WNOHANG);
        if (p == s->apply || now >= s->apply_deadline) {
            int failed = p != s->apply || !WIFEXITED(status) || WEXITSTATUS(status);
            if (p == s->apply)
                s->apply = 0;
            else
                network_stop(&s->apply);
            s->apply_needed = failed;
            s->next = now + (failed ? 2000 : 0);
            if (failed)
                snprintf(s->error, sizeof(s->error), "interface configuration failed");
            else
                s->error[0] = 0;
        }
    }
    if (!d->options.test && s->apply_needed && !s->apply && now >= s->next) {
        network_stop(&s->dhcp);
        pid_t parent = getpid(), pid = fork();
        if (!pid) {
            if (lm_child_setup(parent) || setsid() < 0)
                _exit(126);
            char prefix[8];
            snprintf(prefix, sizeof(prefix), "%lld",
                     (long long)lm_integer(lm_get(s->policy, "prefix"), 24));
            execl("/usr/libexec/leon/network-config", "network-config", s->interface,
                  s->wanted ? lm_string(lm_get(s->policy, "method"), "dhcp") : "disabled",
                  lm_string(lm_get(s->policy, "address"), ""), prefix,
                  lm_string(lm_get(s->policy, "gateway"), ""), NULL);
            _exit(127);
        }
        if (pid > 0) {
            s->apply = pid;
            s->apply_deadline = now + 5000;
        } else {
            s->next = now + 2000;
            snprintf(s->error, sizeof(s->error), "cannot fork interface configuration");
        }
    }
    if (s->dhcp) {
        int status;
        if (waitpid(s->dhcp, &status, WNOHANG) == s->dhcp) {
            s->dhcp = 0;
            s->next = now + 1000 * (1 << (s->failures < 5 ? s->failures : 5));
            if (s->failures < 30)
                ++s->failures;
            snprintf(s->error, sizeof(s->error), "DHCP client exited (%d)",
                     WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
        }
    }
    if (!d->options.test && s->wanted && !s->apply_needed && !s->apply && !s->dhcp &&
        now >= s->next && !strcmp(lm_string(lm_get(s->policy, "method"), ""), "dhcp")) {
        pid_t parent = getpid(), pid = fork();
        if (!pid) {
            if (lm_child_setup(parent) || setsid() < 0)
                _exit(126);
            execl("/sbin/udhcpc", "udhcpc", "-f", "-i", s->interface, "-s",
                  "/usr/libexec/leon/udhcpc", "-t", "5", "-T", "2", "-A", "10", NULL);
            _exit(127);
        }
        if (pid > 0)
            s->dhcp = pid;
        else {
            s->next = now + 2000;
            snprintf(s->error, sizeof(s->error), "cannot fork DHCP client");
        }
    }
    if (now >= s->sample) {
        s->sample = now + 1000;
        if (!d->options.test)
            resolver(d);
        lm_json *out = network_status(d);
        lm_free(out);
    }
}
