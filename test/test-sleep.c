#include "gevent.h"
#include "task2.h"


TEST_IMPL(sleep) {
    int i;
    gevent_hub* hub = gevent_default_hub();
    for (i=0; i<10; ++i) {
        ASSERT_just_main(hub);
        SUCCESS(gevent_sleep(hub, i));
    }
    ASSERT_just_main(hub);
    return 0;
}
