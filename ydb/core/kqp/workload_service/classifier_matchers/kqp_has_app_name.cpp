#include "kqp_has_app_name.h"

#include <library/cpp/regex/pcre/regexp.h>


namespace NKikimr::NKqp::NWorkload {

bool MatchesHasAppName(const std::shared_ptr<TRegExMatch>& compiled, const TString& appName) {
    if (!compiled) {
        return true;
    }
    return compiled->Match(appName.c_str());
}

}  // namespace NKikimr::NKqp::NWorkload
