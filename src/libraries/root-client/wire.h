#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LEON_ROOT_SOCKET "/run/leon-root/control.sock"
#define LEON_ROOT_MESSAGE 8192
void root_clear(void *memory, size_t size);
int root_exchange(const char *, const char *, char *, size_t capacity, unsigned timeout_ms);
int root_connect(const char *path);
int root_send(int fd, const char *text, int descriptor);
int root_receive(int fd, char *text, size_t capacity, int *descriptor);
#ifdef __cplusplus
}
#endif
