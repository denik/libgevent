TEST_DECLARE   (sleep)
TEST_DECLARE   (spawn_noop)
TEST_DECLARE   (spawn_spawn_noop)
TEST_DECLARE   (spawn_sleep)
TEST_DECLARE   (spawn_sleep_many)
TEST_DECLARE   (channel)

TASK_LIST_START
  TEST_ENTRY  (sleep)
  TEST_ENTRY  (spawn_noop)
  TEST_ENTRY  (spawn_spawn_noop)
  TEST_ENTRY  (spawn_sleep)
  TEST_ENTRY  (spawn_sleep_many)
  TEST_ENTRY  (channel)
TASK_LIST_END
