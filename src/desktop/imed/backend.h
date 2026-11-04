#ifndef LEON_IME_BACKEND_H
#define LEON_IME_BACKEND_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ime_backend;
struct ime_backend *ime_backend_open(void);
void ime_backend_close(struct ime_backend *);
int ime_backend_fd(struct ime_backend *);
int ime_backend_poll(struct ime_backend *);
int ime_backend_active(struct ime_backend *);
int ime_backend_private(struct ime_backend *);
unsigned ime_backend_revision(struct ime_backend *);
void ime_backend_send(struct ime_backend *, const char *commit, const char *preedit, int backspace);
#ifdef __cplusplus
}
#endif
#endif
