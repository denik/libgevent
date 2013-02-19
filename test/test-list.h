TEST_DECLARE  (sleep)
TEST_DECLARE  (spawn_noop)
TEST_DECLARE  (spawn_spawn_noop)
TEST_DECLARE  (spawn_sleep)
TEST_DECLARE  (spawn_sleep_many)
TEST_DECLARE  (channel)
TEST_DECLARE  (getaddrinfo_basic)
TEST_DECLARE  (getaddrinfo_concurrent)

TASK_LIST_START
  TEST_ENTRY  (sleep)
  TEST_ENTRY  (spawn_noop)
  TEST_ENTRY  (spawn_spawn_noop)
  TEST_ENTRY  (spawn_sleep)
  TEST_ENTRY  (spawn_sleep_many)
  TEST_ENTRY  (channel)
  TEST_ENTRY  (getaddrinfo_basic)
  TEST_ENTRY  (getaddrinfo_concurrent)
TASK_LIST_END
