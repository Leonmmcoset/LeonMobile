#pragma once
#include <sys/types.h>
void leon_boot_prepare(void);
_Noreturn void leon_shutdown(pid_t manager, int action);
