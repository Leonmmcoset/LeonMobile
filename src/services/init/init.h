#pragma once
#include "boot_log.h"
#include <sys/types.h>
void leon_boot_prepare(void);
void leon_boot_early(void);
int leon_boot_health_tick(void);
_Noreturn void leon_shutdown(pid_t manager, int action);
