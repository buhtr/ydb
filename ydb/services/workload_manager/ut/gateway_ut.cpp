#include <library/cpp/testing/unittest/registar.h>

#include <ydb/services/workload_manager/events.h>
#include <ydb/services/workload_manager/gateway.h>
#include <ydb/services/workload_manager/service/gateway_internal.h>
#include <ydb/services/workload_manager/service/resource_pools_cache_actor.h>
#include <ydb/services/workload_manager/service/service.h>
#include <ydb/services/workload_manager/ut/common/query_classifier_ut_common.h>

#include <ydb/core/kqp/common/simple/services.h>
#include <ydb/core/kqp/runtime/scheduler/kqp_compute_scheduler_service.h>
#include <ydb/core/testlib/basics/appdata.h>
#include <ydb/core/testlib/basics/runtime.h>

#include <ydb/library/actors/core/actor_bootstrapped.h>


namespace NKikimr::NWorkloadManager {

namespace {

class TGatewayHolderActor : public NActors::TActorBootstrapped<TGatewayHolderActor> {
public:
    void Bootstrap() {
        Gateway = CreateGateway(SelfId());
        Become(&TGatewayHolderActor::StateFunc);
    }

    TGatewayPtr Gateway;

private:
    STFUNC(StateFunc) {
        Y_UNUSED(ev);
    }
};

TGatewayHolderActor* SpawnGatewayHolder(NActors::TTestActorRuntime& runtime) {
    auto* holder = new TGatewayHolderActor();
    runtime.Register(holder);
    TDispatchOptions options;
    options.FinalEvents.emplace_back(TEvGatewayResponse::EventType, 1);
    runtime.DispatchEvents(options);
    return holder;
}

}

Y_UNIT_TEST_SUITE(WorkloadManagerGateway) {

    Y_UNIT_TEST(TryCreateQueryClassifierNullBeforeFetch) {
        TTestBasicRuntime runtime(1);
        runtime.Initialize(TAppPrepare().Unwrap());

        auto* holder = new TGatewayHolderActor();
        runtime.Register(holder);
        TDispatchOptions options;
        options.FinalEvents.emplace_back(TEvents::TSystem::Bootstrap, 1);
        runtime.DispatchEvents(options);
        UNIT_ASSERT(holder->Gateway);

        TClassifyContext ctx{
            .PoolId = "",
            .AppName = "",
            .UserToken = nullptr,
        };
        UNIT_ASSERT(!holder->Gateway->TryCreateQueryClassifier(TEST_DB, std::move(ctx)));
    }

    Y_UNIT_TEST(FetchesGatewayFromWorkloadService) {
        TTestBasicRuntime runtime(1);
        TAppPrepare app;
        app.SetEnableResourcePools(true);
        runtime.Initialize(app.Unwrap());
        const ui32 nodeId = runtime.GetNodeId(0);

        const TActorId edge = runtime.AllocateEdgeActor();
        runtime.RegisterService(NKqp::MakeKqpSchedulerServiceId(nodeId), edge);
        auto cacheActor = runtime.Register(CreateResourcePoolsCacheActor(MakeServiceId(nodeId)));
        runtime.RegisterService(MakeServiceId(nodeId), cacheActor);

        auto* holder = SpawnGatewayHolder(runtime);
        UNIT_ASSERT(holder->Gateway);

        auto classifier = runtime.RunCall([&] {
            TClassifyContext ctx{
                .PoolId = "",
                .AppName = "",
                .UserToken = nullptr,
            };
            return holder->Gateway->TryCreateQueryClassifier(TEST_DB, std::move(ctx));
        });
        UNIT_ASSERT(classifier);
    }

    Y_UNIT_TEST(TryCreateQueryClassifierNullWhenPoolsDisabled) {
        TTestBasicRuntime runtime(1);
        TAppPrepare app;
        app.SetEnableResourcePools(false);
        runtime.Initialize(app.Unwrap());
        const ui32 nodeId = runtime.GetNodeId(0);

        const TActorId edge = runtime.AllocateEdgeActor();
        runtime.RegisterService(NKqp::MakeKqpSchedulerServiceId(nodeId), edge);
        auto cacheActor = runtime.Register(CreateResourcePoolsCacheActor(MakeServiceId(nodeId)));
        runtime.RegisterService(MakeServiceId(nodeId), cacheActor);

        auto* holder = SpawnGatewayHolder(runtime);

        TClassifyContext ctx{
            .PoolId = "",
            .AppName = "",
            .UserToken = nullptr,
        };
        UNIT_ASSERT(!holder->Gateway->TryCreateQueryClassifier(TEST_DB, std::move(ctx)));
    }
}

}
