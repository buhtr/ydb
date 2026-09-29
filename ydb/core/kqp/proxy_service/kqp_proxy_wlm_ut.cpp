#include <ydb/core/kqp/common/kqp.h>
#include <ydb/core/kqp/ut/common/kqp_ut_common.h>
#include <ydb/core/testlib/basics/appdata.h>
#include <ydb/core/testlib/test_client.h>
#include <ydb/services/workload_manager/events.h>

#include <library/cpp/testing/unittest/registar.h>

#include <atomic>


namespace NKikimr::NKqp {

using namespace Tests;

namespace {

struct TWlmFixture {
    TPortManager PortManager;
    Tests::TServerSettings Settings;
    Tests::TServer::TPtr Server;
    Tests::TClient Client;
    TTestActorRuntime* Runtime = nullptr;
    TActorId KqpProxy;
    TActorId Sender;

    TWlmFixture()
        : Settings(BuildSettings(PortManager))
        , Server(new Tests::TServer(Settings))
        , Client(Settings)
    {
        Client.InitRootScheme();
        Runtime = Server->GetRuntime();
        KqpProxy = MakeKqpProxyID(Runtime->GetNodeId(0));
        Sender = Runtime->AllocateEdgeActor();
    }

    static Tests::TServerSettings BuildSettings(TPortManager& tp) {
        auto settings = Tests::TServerSettings(tp.GetPort(2134))
            .SetDomainName("Root")
            .SetUseRealThreads(false);
        settings.AppConfig->MutableFeatureFlags()->SetEnableResourcePools(true);
        return settings;
    }
};

TAutoPtr<NKqp::TEvKqp::TEvQueryRequest> MakeSelect42Query(const TString& database) {
    auto ev = MakeHolder<NKqp::TEvKqp::TEvQueryRequest>();
    ev->Record.MutableRequest()->SetAction(NKikimrKqp::QUERY_ACTION_EXECUTE);
    ev->Record.MutableRequest()->SetType(NKikimrKqp::QUERY_TYPE_SQL_GENERIC_QUERY);
    ev->Record.MutableRequest()->SetQuery("SELECT 42;");
    ev->Record.MutableRequest()->SetDatabase(database);
    ev->Record.MutableRequest()->SetKeepSession(false);
    ev->Record.MutableRequest()->SetTimeoutMs(30000);
    return ev.Release();
}

}

Y_UNIT_TEST_SUITE(KqpProxyWorkloadManager) {

    Y_UNIT_TEST(KqpQueryDeferredUntilWlmReady) {
        TWlmFixture fx;

        std::vector<TAutoPtr<IEventHandle>> held;
        fx.Runtime->SetEventFilter([&held](TTestActorRuntimeBase&, TAutoPtr<IEventHandle>& ev) -> bool {
            if (ev->GetTypeRewrite() == NWorkloadManager::TEvWorkloadManagerReady::EventType) {
                held.push_back(ev.Release());
                return true;
            }
            return false;
        });

        fx.Runtime->Send(new IEventHandle(fx.KqpProxy, fx.Sender, MakeSelect42Query("/Root").Release()));

        TDispatchOptions opts;
        opts.FinalEvents.emplace_back([&held](IEventHandle&) { return !held.empty(); });
        fx.Runtime->DispatchEvents(opts);
        UNIT_ASSERT_C(!held.empty(), "Expected proxy to park query waiting for TEvWorkloadManagerReady");

        fx.Runtime->SetEventFilter([](TTestActorRuntimeBase&, TAutoPtr<IEventHandle>&) { return false; });
        for (auto& e : held) {
            fx.Runtime->Send(e.Release());
        }

        auto reply = fx.Runtime->GrabEdgeEventRethrow<NKqp::TEvKqp::TEvQueryResponse>(fx.Sender);
        UNIT_ASSERT_VALUES_EQUAL_C(reply->Get()->Record.GetYdbStatus(), Ydb::StatusIds::SUCCESS,
                                    reply->Get()->Record.GetResponse().GetQueryIssues());
    }

    Y_UNIT_TEST(KqpQueryFailsWhenWlmFetchFails) {
        TWlmFixture fx;

        fx.Runtime->SetEventFilter([runtime = fx.Runtime](TTestActorRuntimeBase&, TAutoPtr<IEventHandle>& ev) -> bool {
            if (ev->GetTypeRewrite() != NWorkloadManager::TEvWorkloadManagerReady::EventType) {
                return false;
            }
            const auto* original = ev->Get<NWorkloadManager::TEvWorkloadManagerReady>();
            // Only rewrite the real reply from the state actor (SUCCESS) — our injected replacement
            // reuses the same event type, so let it through to avoid an infinite filter loop.
            if (original->Status != Ydb::StatusIds::SUCCESS) {
                return false;
            }
            auto* replacement = new NWorkloadManager::TEvWorkloadManagerReady(
                original->Cookie, Ydb::StatusIds::NOT_FOUND, "simulated fetch failure");
            runtime->Send(new IEventHandle(ev->Recipient, ev->Sender, replacement, 0, ev->Cookie));
            return true;
        });

        fx.Runtime->Send(new IEventHandle(fx.KqpProxy, fx.Sender, MakeSelect42Query("/Root").Release()));

        auto reply = fx.Runtime->GrabEdgeEventRethrow<NKqp::TEvKqp::TEvQueryResponse>(fx.Sender);
        UNIT_ASSERT_VALUES_EQUAL(reply->Get()->Record.GetYdbStatus(), Ydb::StatusIds::NOT_FOUND);
    }

    Y_UNIT_TEST(KqpQuerySucceedsOnWlmUnsupportedDb) {
        TWlmFixture fx;

        {
            auto createEv = MakeHolder<NKqp::TEvKqp::TEvQueryRequest>();
            createEv->Record.MutableRequest()->SetAction(NKikimrKqp::QUERY_ACTION_EXECUTE);
            createEv->Record.MutableRequest()->SetType(NKikimrKqp::QUERY_TYPE_SQL_DDL);
            createEv->Record.MutableRequest()->SetQuery("CREATE TABLE `/Root/tbl` (Key Int32, PRIMARY KEY (Key));");
            createEv->Record.MutableRequest()->SetDatabase("/Root");
            fx.Runtime->Send(new IEventHandle(fx.KqpProxy, fx.Sender, createEv.Release()));
            auto ack = fx.Runtime->GrabEdgeEventRethrow<NKqp::TEvKqp::TEvQueryResponse>(fx.Sender);
            UNIT_ASSERT_VALUES_EQUAL_C(ack->Get()->Record.GetYdbStatus(), Ydb::StatusIds::SUCCESS,
                                        ack->Get()->Record.GetResponse().GetQueryIssues());
        }

        fx.Runtime->Send(new IEventHandle(fx.KqpProxy, fx.Sender, MakeSelect42Query("/Root/tbl").Release()));
        auto reply = fx.Runtime->GrabEdgeEventRethrow<NKqp::TEvKqp::TEvQueryResponse>(fx.Sender);
        UNIT_ASSERT_VALUES_EQUAL_C(reply->Get()->Record.GetYdbStatus(), Ydb::StatusIds::SUCCESS,
                                    reply->Get()->Record.GetResponse().GetQueryIssues());
    }

    Y_UNIT_TEST(KqpQuerySendsWlmWarmupOnEntry) {
        TWlmFixture fx;

        std::atomic<int> warmupCount = 0;
        fx.Runtime->SetObserverFunc([&warmupCount](TAutoPtr<IEventHandle>& ev) {
            if (ev->GetTypeRewrite() == NWorkloadManager::TEvWarmupDatabaseInfo::EventType) {
                warmupCount.fetch_add(1);
            }
            return TTestActorRuntime::EEventAction::PROCESS;
        });

        fx.Runtime->Send(new IEventHandle(fx.KqpProxy, fx.Sender, MakeSelect42Query("/Root").Release()));
        auto reply = fx.Runtime->GrabEdgeEventRethrow<NKqp::TEvKqp::TEvQueryResponse>(fx.Sender);
        UNIT_ASSERT_VALUES_EQUAL_C(reply->Get()->Record.GetYdbStatus(), Ydb::StatusIds::SUCCESS,
                                    reply->Get()->Record.GetResponse().GetQueryIssues());

        UNIT_ASSERT_C(warmupCount.load() > 0, "Expected TEvWarmupDatabaseInfo to be sent to the state actor");
    }

}

} // namespace NKikimr::NKqp
