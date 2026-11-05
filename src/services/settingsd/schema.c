#include "settings.h"
#include <string.h>
static const char *keys[] = {"dark", "reduce_motion", "language", "navigation", "volume"};
lm_json *settings_defaults(void) {
    lm_json *j = lm_object();
    lm_set_boolean(j, "dark", 0);
    lm_set_boolean(j, "reduce_motion", 0);
    lm_set_string(j, "language", "zh-CN");
    lm_set_string(j, "navigation", "gestures");
    lm_set_integer(j, "volume", 70);
    return j;
}
int settings_validate(const lm_json *j, int complete) {
    if (!lm_is_object(j) || lm_size(j) > 5 || (complete && lm_size(j) != 5))
        return 0;
    size_t found = 0;
    for (size_t i = 0; i < 5; ++i) {
        const lm_json *v = lm_get(j, keys[i]);
        if (!v)
            continue;
        ++found;
        if (i < 2) {
            if (lm_boolean(v, -1) < 0)
                return 0;
        } else if (i < 4) {
            const char *s = lm_string(v, "");
            if (i == 2 ? strcmp(s, "zh-CN") && strcmp(s, "en")
                       : strcmp(s, "gestures") && strcmp(s, "buttons"))
                return 0;
        } else {
            int64_t n = lm_integer(v, -1);
            if (n < 0 || n > 100)
                return 0;
        }
    }
    return found == lm_size(j);
}
lm_json *settings_schema(void) {
    const char *text = "{\"dark\":{\"type\":\"boolean\",\"default\":false},"
        "\"reduce_motion\":{\"type\":\"boolean\",\"default\":false},"
        "\"language\":{\"type\":\"string\",\"default\":\"zh-CN\",\"choices\":[\"zh-CN\",\"en\"]},"
        "\"navigation\":{\"type\":\"string\",\"default\":\"gestures\",\"choices\":[\"gestures\",\"buttons\"]},"
        "\"volume\":{\"type\":\"integer\",\"default\":70,\"min\":0,\"max\":100}}";
    return lm_parse(text, strlen(text));
}
