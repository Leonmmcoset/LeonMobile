#include "time_state.h"
#include <assert.h>
#include <stdlib.h>
#include <time.h>
int main(void) {
    time_t value;
    setenv("TZ","Etc/UTC",1);tzset();
    assert(!time_local_parse("2024-02-29 12:34:56",&value));
    assert(value == 1709210096);
    const char *invalid[]={"2023-02-29 12:00:00","2024-04-31 12:00:00","2024-13-01 00:00:00",
        "2024-01-01 24:00:00","2024-01-01 00:00:60","2019-12-31 23:59:59",
        "2024-01-01","2024-1-01 00:00:00","2024-01-01 00:00:00x"};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) assert(time_local_parse(invalid[i],&value));
    setenv("TZ","America/New_York",1);tzset();
    assert(time_local_parse("2024-03-10 02:30:00",&value));
    assert(time_local_parse("2024-11-03 01:30:00",&value));
    assert(!time_local_parse("2024-11-03 03:30:00",&value));
    return 0;
}
