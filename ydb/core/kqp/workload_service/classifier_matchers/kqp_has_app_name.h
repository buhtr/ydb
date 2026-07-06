#pragma once

#include <util/generic/string.h>

#include <memory>


class TRegExMatch;


namespace NKikimr::NKqp::NWorkload {

//
// Returns true iff the HAS_APP_NAME predicate matches.
//   compiled: pre-compiled regex (nullptr = predicate not set, always matches)
//   appName:  the actual app name carried by the request
//
bool MatchesHasAppName(const std::shared_ptr<TRegExMatch>& compiled, const TString& appName);

}  // namespace NKikimr::NKqp::NWorkload
