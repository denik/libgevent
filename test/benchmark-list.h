BENCHMARK_DECLARE (million_spawns_noop)
BENCHMARK_DECLARE (million_spawns_sleep0)
BENCHMARK_DECLARE (channel_pingpong)

TASK_LIST_START
  BENCHMARK_ENTRY  (million_spawns_noop)
  BENCHMARK_ENTRY  (million_spawns_sleep0)
  BENCHMARK_ENTRY  (channel_pingpong)
TASK_LIST_END
