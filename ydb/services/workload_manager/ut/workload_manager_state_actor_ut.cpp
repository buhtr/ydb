#include <library/cpp/testing/unittest/registar.h>

#include <ydb/services/workload_manager/actors/workload_manager_state_actor.h>
#include <ydb/services/workload_manager/events.h>
#include <ydb/services/workload_manager/gateway.h>
#include <ydb/services/workload_manager/gateway_internal.h>
#include <ydb/services/workload_manager/service/service.h>
#include <ydb/services/workload_manager/ut/common/query_classifier_ut_common.h>

#include <ydb/core/base/appdata.h>
#include <ydb/core/kqp/common/simple/services.h>
#include <ydb/core/kqp/runtime/scheduler/kqp_compute_scheduler_service.h>
#include <ydb/core/testlib/basics/appdata.h>
#include <ydb/core/testlib/basics/runtime.h>
#include <ydb/core/tx/scheme_cache/scheme_cache.h>


namespace NKikimr::NWorkloadManager {

namespace {

constexpr TDuration WAIT_TIMEOUT = TDuration::Seconds(10);
constexpr TDuration NEGATIVE_WAIT_TIMEOUT = TDuration::MilliSeconds(200);

struct TFixture {
    TTestBasicRuntime Runtime{1};
    std::shared_ptr<NPrivate::TWorkloadManagerGateway> Gateway;
    TActorId SchemeCacheEdge;
    TActorId ServicesEdge;
    TActorId StateActor;
    ui32 NodeId = 0;

    void Init(bool enableResourcePools = true) {
        TAppPrepare app;
        app.SetEnableResourcePools(enableResourcePools);
        Runtime.Initialize(app.Unwrap());
        NodeId = Runtime.GetNodeId(0);

        Gateway = std::make_shared<NPrivate::TWorkloadManagerGateway>();
        Runtime.GetAppData().WorkloadManagerGateway = Gateway;

        SchemeCacheEdge = Runtime.AllocateEdgeActor();
        ServicesEdge = Runtime.AllocateEdgeActor();
        Runtime.RegisterService(MakeSchemeCacheID(), SchemeCacheEdge);
        Runtime.RegisterService(NKqp::MakeKqpSchedulerServiceId(NodeId), ServicesEdge);
        Runtime.RegisterService(MakeServiceId(NodeId), ServicesEdge);

        StateActor = Runtime.Register(CreateWorkloadManagerStateActor(Gateway));

        TDispatchOptions options;
        options.FinalEvents.emplace_back(TEvents::TSystem::Bootstrap, 1);
        Runtime.DispatchEvents(options);
    }

    void InjectFetchResponse(const TString& databasePath, const TString& databaseId,
                             bool serverless, TPathId pathId,
                             Ydb::StatusIds::StatusCode status = Ydb::StatusIds::SUCCESS,
                             NYql::TIssues issues = {}) {
        Runtime.Send(new IEventHandle(
            StateActor, {},
            new TEvFetchDatabaseResponse(status, databasePath, databaseId, serverless, pathId, std::move(issues))));
    }
};

TReadyInfo EnsureReady(TFixture& fx, const TString& databaseId) {
    return fx.Runtime.RunCall([databaseId] {
        return AppData()->WorkloadManagerGateway->EnsureReady(databaseId);
    });
}

}

Y_UNIT_TEST_SUITE(WorkloadManagerStateActor) {

    Y_UNIT_TEST(TestEnssureWhenPoolsDisabled) {
        TFixture fx;
        fx.Init(/*enableResourcePools=*/false);

        auto info = EnsureReady(fx, TEST_DB);
        UNIT_ASSERT(info.State == EReadyState::ClassificationDisabled);
    }

    Y_UNIT_TEST(TestEnsureWhenPoolsEnabled) {
        TFixture fx;
        fx.Init();

        fx.InjectFetchResponse("/Root/db1", "/Root/db1", /*serverless=*/false, TPathId(1, 1));
        fx.Runtime.DispatchEvents({});

        auto info = EnsureReady(fx, "/Root/db1");
        UNIT_ASSERT(info.State == EReadyState::Ready);
    }

    Y_UNIT_TEST(TestWarmupSpawnsFetcher) {
        TFixture fx;
        fx.Init();

        fx.Runtime.Send(new IEventHandle(
            fx.StateActor, {}, new TEvWarmupDatabaseInfo("/Root/db1")));

        auto ev = fx.Runtime.GrabEdgeEvent<TEvTxProxySchemeCache::TEvNavigateKeySet>(fx.SchemeCacheEdge, WAIT_TIMEOUT);
        UNIT_ASSERT(ev);
    }

    Y_UNIT_TEST(TestWarmupDedupsByPath) {
        TFixture fx;
        fx.Init();

        fx.Runtime.Send(new IEventHandle(
            fx.StateActor, {}, new TEvWarmupDatabaseInfo("/Root/db1")));
        fx.Runtime.Send(new IEventHandle(
            fx.StateActor, {}, new TEvWarmupDatabaseInfo("/Root/db1")));

        auto first = fx.Runtime.GrabEdgeEvent<TEvTxProxySchemeCache::TEvNavigateKeySet>(fx.SchemeCacheEdge, WAIT_TIMEOUT);
        UNIT_ASSERT(first);

        auto second = fx.Runtime.GrabEdgeEvent<TEvTxProxySchemeCache::TEvNavigateKeySet>(fx.SchemeCacheEdge, NEGATIVE_WAIT_TIMEOUT);
        UNIT_ASSERT_C(!second, "Second Warmup for the same path should not spawn a fetcher");
    }

    Y_UNIT_TEST(TestWarmupIgnoresEmptyPath) {
        TFixture fx;
        fx.Init();

        fx.Runtime.Send(new IEventHandle(
            fx.StateActor, {}, new TEvWarmupDatabaseInfo("")));

        auto ev = fx.Runtime.GrabEdgeEvent<TEvTxProxySchemeCache::TEvNavigateKeySet>(fx.SchemeCacheEdge, NEGATIVE_WAIT_TIMEOUT);
        UNIT_ASSERT(!ev);
    }

    Y_UNIT_TEST(TestSubscribeWhenAlreadyKnown) {
        TFixture fx;
        fx.Init();

        fx.InjectFetchResponse("/Root/db1", "/Root/db1", /*serverless=*/false, TPathId(1, 1));
        fx.Runtime.DispatchEvents({});

        const TActorId subscriber = fx.Runtime.AllocateEdgeActor();
        fx.Runtime.Send(new IEventHandle(
            fx.StateActor, subscriber,
            new TEvSubscribeOnWorkloadManagerReady("/Root/db1", subscriber, /*cookie=*/42)));

        auto ev = fx.Runtime.GrabEdgeEvent<TEvWorkloadManagerReady>(subscriber, WAIT_TIMEOUT);
        UNIT_ASSERT(ev);
        UNIT_ASSERT_VALUES_EQUAL(ev->Get()->Cookie, 42u);
        UNIT_ASSERT_EQUAL(ev->Get()->Status, Ydb::StatusIds::SUCCESS);
    }

    Y_UNIT_TEST(TestSubscribeWhenNotKnow) {
        TFixture fx;
        fx.Init();

        const TActorId subscriber = fx.Runtime.AllocateEdgeActor();
        fx.Runtime.Send(new IEventHandle(
            fx.StateActor, subscriber,
            new TEvSubscribeOnWorkloadManagerReady("/Root/db1", subscriber, /*cookie=*/7)));

        auto navigate = fx.Runtime.GrabEdgeEvent<TEvTxProxySchemeCache::TEvNavigateKeySet>(fx.SchemeCacheEdge, WAIT_TIMEOUT);
        UNIT_ASSERT_C(navigate, "SubscribeOnReady should trigger a fetch when info is missing");

        fx.InjectFetchResponse("/Root/db1", "/Root/db1", /*serverless=*/false, TPathId(1, 1));

        auto ready = fx.Runtime.GrabEdgeEvent<TEvWorkloadManagerReady>(subscriber, WAIT_TIMEOUT);
        UNIT_ASSERT(ready);
        UNIT_ASSERT_VALUES_EQUAL(ready->Get()->Cookie, 7u);
        UNIT_ASSERT_EQUAL(ready->Get()->Status, Ydb::StatusIds::SUCCESS);
    }

    Y_UNIT_TEST(TestSubscribeWhenError) {
        TFixture fx;
        fx.Init();

        const TActorId subscriber = fx.Runtime.AllocateEdgeActor();
        fx.Runtime.Send(new IEventHandle(
            fx.StateActor, subscriber,
            new TEvSubscribeOnWorkloadManagerReady("/Root/db1", subscriber, /*cookie=*/9)));

        fx.Runtime.GrabEdgeEvent<TEvTxProxySchemeCache::TEvNavigateKeySet>(fx.SchemeCacheEdge, WAIT_TIMEOUT);

        NYql::TIssues issues;
        issues.AddIssue(NYql::TIssue("scheme cache miss"));
        fx.InjectFetchResponse("/Root/db1", "/Root/db1", /*serverless=*/false, TPathId(), Ydb::StatusIds::NOT_FOUND, issues);

        auto ready = fx.Runtime.GrabEdgeEvent<TEvWorkloadManagerReady>(subscriber, WAIT_TIMEOUT);
        UNIT_ASSERT(ready);
        UNIT_ASSERT_VALUES_EQUAL(ready->Get()->Cookie, 9u);
        UNIT_ASSERT_EQUAL(ready->Get()->Status, Ydb::StatusIds::NOT_FOUND);
        UNIT_ASSERT_STRING_CONTAINS(ready->Get()->Message, "scheme cache miss");

        auto info = EnsureReady(fx, "/Root/db1");
        UNIT_ASSERT(info.State == EReadyState::Failed);
        UNIT_ASSERT_EQUAL(info.FailureStatus, Ydb::StatusIds::NOT_FOUND);
    }

    Y_UNIT_TEST(TestEnsureWhenPoolsDisabledOnServerless) {
        TFixture fx;
        fx.Init();

        fx.InjectFetchResponse("/Root/db1", "/Root/db1", /*serverless=*/true, TPathId(1, 1));
        fx.Runtime.DispatchEvents({});

        auto info = EnsureReady(fx, "/Root/db1");
        UNIT_ASSERT(info.State == EReadyState::ClassificationDisabled);
    }
}

}
