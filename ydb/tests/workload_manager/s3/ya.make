PY3TEST()

TEST_SRCS(
    test_compute_scheduler.py
)

FORK_TESTS()
FORK_TEST_FILES()
FORK_SUBTESTS()

REQUIREMENTS(ram:16 cpu:4)

IF (SANITIZER_TYPE)
    SIZE(LARGE)
    INCLUDE(${ARCADIA_ROOT}/ydb/tests/large.inc)
ELSE()
    SIZE(MEDIUM)
ENDIF()

ENV(YDB_ENABLE_COLUMN_TABLES="true")
INCLUDE(${ARCADIA_ROOT}/ydb/tests/harness_dep.inc)
ENV(YDB_CLI_BINARY="ydb/apps/ydb/ydb")
ENV(NO_KUBER_LOGS="yes")
ENV(WAIT_CLUSTER_ALIVE_TIMEOUT="60")

PEERDIR(
    ydb/tests/workload_manager/s3/lib
)

DEPENDS(
    ydb/apps/ydb
)

END()
