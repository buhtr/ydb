#include "predicate_compile.h"

#include <library/cpp/regex/pcre/regexp.h>

#include <util/string/builder.h>


namespace NKikimr::NKqp {

std::shared_ptr<TRegExMatch> CompilePredicateRegex(TStringBuf pattern) {
    TString anchored = TStringBuilder() << "^(?:" << pattern << ")$";
    try {
        return std::make_shared<TRegExMatch>(anchored, REG_EXTENDED | REG_NOSUB);
    } catch (...) {
        return nullptr;
    }
}

}  // namespace NKikimr::NKqp
