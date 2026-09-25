#pragma once

#include <ydb/services/workload_manager/gateway.h>
#include <ydb/services/workload_manager/metadata_subscription/resource_pool_classifier/snapshot.h>

#include <ydb/library/actors/core/actorid.h>

#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/system/rwlock.h>

#include <memory>


namespace NKikimr::NWorkloadManager::NPrivate {

struct TDatabaseInfo {
    bool Serverless = false;
};

///
/// Snapshot of the workload manager state. Immutable once published.
///
struct TSnapshot {
    TResourcePoolMapPtr Pools;
    std::shared_ptr<const TResourcePoolClassifierSnapshot> Classifiers;
    THashMap<TString, TDatabaseInfo> Databases;
    bool EnableResourcePools = false;
    bool EnableResourcePoolsOnServerless = false;

    bool IsResourcePoolsEnabled(const TString& databaseId) const {
        if (!EnableResourcePools) {
            return false;
        }
        if (EnableResourcePoolsOnServerless) {
            return true;
        }
        const auto it = Databases.find(databaseId);
        return it == Databases.end() || !it->second.Serverless;
    }
};

using TSnapshotPtr = std::shared_ptr<const TSnapshot>;

///
/// Server-side implementation of IGateway. Created in the initializer and
/// stored in `AppData()->WorkloadManagerGateway`. Cache actor writes
/// snapshots via `PublishSnapshot`; consumers call `TryCreateQueryClassifier`.
///
class TWorkloadManagerGateway : public IGateway {
public:
    void OnRegistered(NActors::TActorId cacheActorId) {
        CacheActorId_ = cacheActorId;
    }

    void PublishSnapshot(TSnapshotPtr snapshot) {
        TWriteGuard guard(Lock_);
        Snapshot_ = std::move(snapshot);
    }

    std::shared_ptr<IQueryClassifier> TryCreateQueryClassifier(
        const TString& databaseId, TClassifyContext context) override;

private:
    mutable TRWMutex Lock_;
    TSnapshotPtr Snapshot_;
    NActors::TActorId CacheActorId_;
};

}
