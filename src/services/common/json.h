#ifndef LEON_SYSTEM_JSON_H
#define LEON_SYSTEM_JSON_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct lm_json lm_json;
lm_json *lm_object(void);
lm_json *lm_array(void);
lm_json *lm_parse(const char *text, size_t size);
char *lm_dump(const lm_json *json);
void lm_free(lm_json *json);
const lm_json *lm_get(const lm_json *json, const char *key);
const lm_json *lm_at(const lm_json *json, size_t index);
size_t lm_size(const lm_json *json);
const char *lm_string(const lm_json *json, const char *fallback);
int64_t lm_integer(const lm_json *json, int64_t fallback);
int lm_boolean(const lm_json *json, int fallback);
int lm_is_array(const lm_json *json);
int lm_is_object(const lm_json *json);
int lm_is_string(const lm_json *json);
void lm_set(lm_json *json, const char *key, lm_json *value);
void lm_append(lm_json *json, lm_json *value);
void lm_set_string(lm_json *json, const char *key, const char *value);
void lm_set_integer(lm_json *json, const char *key, int64_t value);
void lm_set_boolean(lm_json *json, const char *key, int value);
lm_json *lm_copy(const lm_json *json);
/* Shared package identity/manifest checks, returning owned values. */
lm_json *lm_app_by_id(const char *root, const char *builtin, const char *id);
int lm_grants_valid(const lm_json *json);
lm_json *lm_app(const char *root, const char *builtin, uid_t uid);
lm_json *lm_apps(const char *root, const char *builtin);
lm_json *lm_root_app(const char *root, const char *builtin, uid_t uid, pid_t pid);
#ifdef __cplusplus
}
#endif
#endif
