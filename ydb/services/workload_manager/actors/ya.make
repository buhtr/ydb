LIBRARY()

SRCS(
    cpu_load_actors.cpp
    pool_handlers_actors.cpp
    resource_pools_cache_actor.cpp
    scheme_actors.cpp
)

PEERDIR(
    ydb/services/workload_manager
    ydb/services/workload_manager/common
    ydb/services/workload_manager/metadata_subscription/resource_pool_classifier
    ydb/services/workload_manager/tables

    ydb/core/base
    ydb/core/cms/console
    ydb/core/kqp/common
    ydb/core/kqp/common/events
    ydb/core/kqp/runtime
    ydb/core/protos
    ydb/core/resource_pools
    ydb/core/tx/tx_proxy

    ydb/services/metadata
)

YQL_LAST_ABI_VERSION()

END()
