#include "gevent.h"

int main() {
    gevent_hub* hub = gevent_default_hub();
    gevent_sleep(hub, 500);
    return 0;
}
