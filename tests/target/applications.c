#include "system_probe.h"
#include <stdio.h>

// Disposable-image test client. It keeps the invoking console's real UID and
// provides observations independent of Settings' rendered state.
int main(int argc, char **argv) {
    if (argc != 2 || probe_rpc("apps", argv[1], 0))
        return 1;
    puts(probe_reply());
    return 0;
}
