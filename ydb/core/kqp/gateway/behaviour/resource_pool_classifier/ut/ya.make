UNITTEST_FOR(ydb/core/kqp/gateway/behaviour/resource_pool_classifier)

SRCS(
    predicate_compile_ut.cpp
)

PEERDIR(
    library/cpp/regex/pcre
    library/cpp/testing/unittest
)

YQL_LAST_ABI_VERSION()

END()
