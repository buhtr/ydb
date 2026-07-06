#include "predicate_compile.h"

#include <library/cpp/regex/pcre/regexp.h>
#include <library/cpp/testing/unittest/registar.h>


namespace NKikimr::NKqp {

namespace {

std::shared_ptr<TRegExMatch> Compile(TStringBuf pattern) {
    auto compiled = CompilePredicateRegex(pattern);
    UNIT_ASSERT_C(compiled, TStringBuilder() << "expected pattern '" << pattern << "' to compile");
    return compiled;
}

}  // anonymous namespace


Y_UNIT_TEST_SUITE(TPredicateCompile) {

    Y_UNIT_TEST(InvalidPatternYieldsNullptr) {
        UNIT_ASSERT(!CompilePredicateRegex("[unclosed"));
        UNIT_ASSERT(!CompilePredicateRegex("(?<bad"));
    }

    Y_UNIT_TEST(EmptyPatternCompiles) {
        auto compiled = Compile("");
        UNIT_ASSERT(compiled->Match(""));
        UNIT_ASSERT(!compiled->Match("x"));
    }

    Y_UNIT_TEST(LiteralIsAnchored) {
        auto compiled = Compile("ydb-ui");
        UNIT_ASSERT(compiled->Match("ydb-ui"));
        UNIT_ASSERT(!compiled->Match("ydb-ui-prod"));
        UNIT_ASSERT(!compiled->Match("prefix-ydb-ui"));
    }

    Y_UNIT_TEST(AlternationMatchesExactlyEachBranch) {
        // (?:...) wrap rescues us from `^a|b$` precedence.
        auto compiled = Compile("ydb-ui|ydb-cli");
        UNIT_ASSERT(compiled->Match("ydb-ui"));
        UNIT_ASSERT(compiled->Match("ydb-cli"));
        UNIT_ASSERT(!compiled->Match("ydb-ui-prod"));
        UNIT_ASSERT(!compiled->Match("prefix-ydb-cli"));
    }

    Y_UNIT_TEST(StarMatchesSuffix) {
        auto compiled = Compile("ydb-.*");
        UNIT_ASSERT(compiled->Match("ydb-ui"));
        UNIT_ASSERT(compiled->Match("ydb-cli-v2"));
        UNIT_ASSERT(!compiled->Match("ydb"));
        UNIT_ASSERT(!compiled->Match("other-ydb-ui"));
    }

    Y_UNIT_TEST(AdminProvidedAnchorsAreHarmless) {
        auto compiled = Compile("^ydb-ui$");
        UNIT_ASSERT(compiled->Match("ydb-ui"));
        UNIT_ASSERT(!compiled->Match("ydb-ui-prod"));
    }
}

}  // namespace NKikimr::NKqp
