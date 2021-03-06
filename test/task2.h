#include "task.h"

#define SUCCESS(expr)                                                 \
 do {                                                                 \
  if (expr) {                                                         \
    fprintf(stderr,                                                   \
            "Assertion failed in %s on line %d: %s: %s: %s\n",        \
            __FILE__,                                                 \
            __LINE__,                                                 \
            #expr,                                                    \
            uv_err_name(uv_last_error(uv_default_loop())),            \
            uv_strerror(uv_last_error(uv_default_loop())));           \
    abort();                                                          \
  }                                                                   \
 } while (0)


#define ASSERT_just_main(HUB)                                \
    ASSERT(hub->current->state == GEVENT_COTHREAD_CURRENT);  \
    ASSERT(hub->current == &hub->main);                      \
    ASSERT(ngx_queue_empty(&hub->ready));

