#include "internal.h"

#include <ydb/services/workload_manager/gateway.h>
#include <ydb/services/workload_manager/service/service.h>

#include <ydb/library/actors/core/actor.h>
#include <ydb/library/actors/core/actor_coroutine.h>

#include <util/system/spinlock.h>

#include <memory>


namespace NKikimr::NWorkloadManager {

namespace {

///
/// Client-side proxy for IGateway. Starts empty.
///
class TGatewayProxy : public IGateway {
public:
    std::shared_ptr<IQueryClassifier> TryCreateQueryClassifier(
        const TString& databaseId, TClassifyContext context) override
    {
        TGatewayPtr delegate;
        with_lock (Lock_) {
            delegate = Delegate_;
        }
        if (!delegate) {
            return nullptr;
        }
        return delegate->TryCreateQueryClassifier(databaseId, std::move(context));
    }

    void SetDelegate(TGatewayPtr delegate) {
        with_lock (Lock_) {
            Delegate_ = std::move(delegate);
        }
    }

private:
    TAdaptiveLock Lock_;
    TGatewayPtr Delegate_;
};

///
/// One-shot coroutine: sends `TEvGetGateway` to WLM service, installs the
/// received gateway into `TGatewayProxy` via `SetDelegate`.
///
class TFetcherActor : public NActors::TActorCoroImpl {
public:
    explicit TFetcherActor(std::shared_ptr<TGatewayProxy> gateway)
        : NActors::TActorCoroImpl(16_KB)
        , Gateway_(std::move(gateway))
    {}

    void Run() override {
        Send(NWorkloadManager::MakeServiceId(SelfActorId.NodeId()),
             new TEvGetGateway());
        auto ev = WaitForSpecificEvent<TEvGatewayResponse>(
            [](TAutoPtr<NActors::IEventHandle>) {},
            NActors::TActivationContext::Monotonic() + TDuration::Seconds(30));
        if (ev) {
            Gateway_->SetDelegate(std::move(ev->Get()->Gateway));
        }
    }

private:
    std::shared_ptr<TGatewayProxy> Gateway_;
};

}

TGatewayPtr CreateGateway(NActors::TActorId owner) {
    auto gateway = std::make_shared<TGatewayProxy>();
    NActors::TActivationContext::Register(
        new NActors::TActorCoro(MakeHolder<TFetcherActor>(gateway)),
        owner);
    return gateway;
}

}
