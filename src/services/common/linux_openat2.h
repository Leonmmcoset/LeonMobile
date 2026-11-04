#pragma once
#include <stdint.h>
#include <sys/syscall.h>

/* Linux openat2 UAPI for the pinned musl SDK's older Linux headers. */
struct open_how {
    uint64_t flags, mode, resolve;
};
#define RESOLVE_NO_MAGICLINKS 0x02
#define RESOLVE_NO_SYMLINKS 0x04
#define RESOLVE_BENEATH 0x08
#ifndef SYS_openat2
#define SYS_openat2 437
#endif
