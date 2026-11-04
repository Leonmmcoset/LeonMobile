#include "shell.hpp"
#include <sys/prctl.h>
#include <sys/resource.h>
int main() {
    rlimit core{0, 0};
    setrlimit(RLIMIT_CORE, &core);
    prctl(PR_SET_DUMPABLE, 0);
    leon::shell::Shell shell;
    return shell.run();
}
