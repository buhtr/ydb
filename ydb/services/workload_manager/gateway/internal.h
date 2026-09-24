#pragma once

#include <ydb/services/workload_manager/events.h>
#include <ydb/services/workload_manager/gateway.h>
#include <ydb/services/workload_manager/metadata_subscription/resource_pool_classifier/snapshot.h>

#include <ydb/core/protos/feature_flags.pb.h>
#include <ydb/core/protos/workload_manager_config.pb.h>

#include <ydb/library/actors/core/actorid.h>
#include <ydb/library/actors/core/event_local.h>

#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/system/spinlock.h>

#include <memory>


namespace NKikimr::NWorkloadManager {

struct TEvGetGateway : NActors::TEventLocal<TEvGetGateway, TWorkloadManagerEvents::EvGetGateway> {
};

struct TEvGatewayResponse : NActors::TEventLocal<TEvGatewayResponse, TWorkloadManagerEvents::EvGatewayResponse> {
    TGatewayPtr Gateway;
    explicit TEvGatewayResponse(TGatewayPtr gateway)
        : Gateway(std::move(gateway))
    {}
};

}


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
    NKikimrConfig::TFeatureFlags FeatureFlags;
    NKikimrConfig::TWorkloadManagerConfig WorkloadManagerConfig;
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

}
