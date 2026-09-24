#pragma once

#include <ydb/services/workload_manager/query_classifier.h>

#include <ydb/library/actors/core/actorid.h>

#include <memory>


namespace NKikimr::NWorkloadManager {

///
/// Client-side interface for the Workload Manager.
///
class IGateway {
public:
    virtual ~IGateway() = default;

    ///
    /// Attempt to build a Query Classifier for the given database
    /// and query context. Returns `nullptr` when:
    /// - gateway does not fetched yet;
    /// - resource pools are disabled for the database;
    /// - no matching pool metadata is loaded.
    /// Caller must handle the nullptr case (typically: skip classification).
    /// 
    virtual std::shared_ptr<IQueryClassifier> TryCreateQueryClassifier(
        const TString& databaseId, TClassifyContext context) = 0;
};

using TGatewayPtr = std::shared_ptr<IGateway>;

///
/// Create a gateway bound to `owner`. Returns immediately; the actual
/// snapshot is fetched asynchronously in the background
///
TGatewayPtr CreateGateway(NActors::TActorId owner);

}
