#include "kqp_query_classifier_ut_common.h"

#include <ydb/core/kqp/gateway/behaviour/resource_pool_classifier/predicate_compile.h>
#include <ydb/core/kqp/workload_service/classifier_matchers/kqp_has_app_name.h>

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


Y_UNIT_TEST_SUITE(TClassifierMatcherHasAppName) {

    Y_UNIT_TEST(NullPatternMatchesAnything) {
        UNIT_ASSERT(NWorkload::MatchesHasAppName(nullptr, ""));
        UNIT_ASSERT(NWorkload::MatchesHasAppName(nullptr, "ydb-ui"));
    }

    Y_UNIT_TEST(LiteralPattern) {
        auto compiled = Compile("ydb-ui");
        UNIT_ASSERT(NWorkload::MatchesHasAppName(compiled, "ydb-ui"));
        UNIT_ASSERT(!NWorkload::MatchesHasAppName(compiled, "ydb-cli"));
        UNIT_ASSERT(!NWorkload::MatchesHasAppName(compiled, "ydb-ui-prod"));
    }

    Y_UNIT_TEST(AlternationPattern) {
        auto compiled = Compile("ydb-ui|ydb-cli");
        UNIT_ASSERT(NWorkload::MatchesHasAppName(compiled, "ydb-ui"));
        UNIT_ASSERT(NWorkload::MatchesHasAppName(compiled, "ydb-cli"));
        UNIT_ASSERT(!NWorkload::MatchesHasAppName(compiled, "other"));
    }

    Y_UNIT_TEST(EmptyContextDoesNotMatchNonEmptyPattern) {
        auto compiled = Compile("ydb-ui");
        UNIT_ASSERT(!NWorkload::MatchesHasAppName(compiled, ""));
    }
}


Y_UNIT_TEST_SUITE(TQueryClassifierHasAppName) {

    Y_UNIT_TEST(ShouldMatchAppName) {
        TClassifyTestCase tc;
        tc.ClassifierHasAppName = "my_app";
        tc.ContextAppName = "my_app";
        UNIT_ASSERT_VALUES_EQUAL(GetPoolId(tc.RunPreClassify()), "pool_target");
    }

    Y_UNIT_TEST(ShouldNotMatchDifferentAppName) {
        TClassifyTestCase tc;
        tc.ClassifierHasAppName = "expected_app";
        tc.ContextAppName = "other_app";
        UNIT_ASSERT_VALUES_EQUAL(GetPoolId(tc.RunPreClassify()), "default");
    }

    Y_UNIT_TEST(ShouldMatchAnyAppWhenFilterNotSet) {
        TClassifyTestCase tc;
        tc.ContextAppName = "some_random_app";
        UNIT_ASSERT_VALUES_EQUAL(GetPoolId(tc.RunPreClassify()), "pool_target");
    }

    Y_UNIT_TEST(ShouldMatchCombinedAppNameAndMemberName) {
        TClassifyTestCase tc;
        tc.ClassifierHasAppName = "my_app";
        tc.ClassifierMemberName = "alice";
        tc.ContextAppName = "my_app";
        tc.ContextMemberName = "alice";
        UNIT_ASSERT_VALUES_EQUAL(GetPoolId(tc.RunPreClassify()), "pool_target");
    }

    Y_UNIT_TEST(ShouldMatchAppNameRegexSuffix) {
        TClassifyTestCase tc;
        tc.ClassifierHasAppName = "ydb-.*";
        tc.ContextAppName = "ydb-ui";
        UNIT_ASSERT_VALUES_EQUAL(GetPoolId(tc.RunPreClassify()), "pool_target");
    }

    Y_UNIT_TEST(ShouldMatchAppNameAlternation) {
        TClassifyTestCase tc;
        tc.ClassifierHasAppName = "ydb-ui|ydb-cli";
        tc.ContextAppName = "ydb-cli";
        UNIT_ASSERT_VALUES_EQUAL(GetPoolId(tc.RunPreClassify()), "pool_target");
    }

    Y_UNIT_TEST(ShouldRejectLiteralBeyondAnchor) {
        TClassifyTestCase tc;
        tc.ClassifierHasAppName = "ydb-ui";
        tc.ContextAppName = "ydb-ui-prod";
        UNIT_ASSERT_VALUES_EQUAL(GetPoolId(tc.RunPreClassify()), "default");
    }
}

}  // namespace NKikimr::NKqp
