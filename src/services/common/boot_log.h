#pragma once

/* Internal PID 1 / supervisor diagnostics. Serial stderr always remains active.
 * Explicitly enable the optional, best-effort mirror on the boot VT. */
void leon_boot_console_enable(void);
void leon_boot_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
void leon_boot_error(const char *label);
