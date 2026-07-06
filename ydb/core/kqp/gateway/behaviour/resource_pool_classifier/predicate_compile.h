#pragma once

#include <util/generic/strbuf.h>

#include <memory>


class TRegExMatch;


namespace NKikimr::NKqp {

//
// Compiles a classifier predicate pattern (regex with implicit anchoring).
//
// The pattern is wrapped with ^(?:...)$ before compilation: the matcher is a
// full-string match, and the non-capturing group neutralises | precedence so
// `ydb-ui|ydb-cli` matches exactly those two literals.
//
// Returns nullptr if the wrapped pattern fails to compile.
//
std::shared_ptr<TRegExMatch> CompilePredicateRegex(TStringBuf pattern);

}  // namespace NKikimr::NKqp
