// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/speculation_rules/speculation_rule_set.h"

#include "base/bind.h"
#include "base/ranges/algorithm.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/mock_callback.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/browser_interface_broker_proxy.h"
#include "third_party/blink/public/mojom/speculation_rules/speculation_rules.mojom-blink.h"
#include "third_party/blink/public/web/blink.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_union_urlpatterninit_usvstring.h"
#include "third_party/blink/renderer/core/execution_context/agent.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/web_feature.h"
#include "third_party/blink/renderer/core/html/html_anchor_element.h"
#include "third_party/blink/renderer/core/html/html_head_element.h"
#include "third_party/blink/renderer/core/html/html_script_element.h"
#include "third_party/blink/renderer/core/loader/empty_clients.h"
#include "third_party/blink/renderer/core/speculation_rules/document_rule_predicate.h"
#include "third_party/blink/renderer/core/speculation_rules/document_speculation_rules.h"
#include "third_party/blink/renderer/core/speculation_rules/stub_speculation_host.h"
#include "third_party/blink/renderer/core/testing/dummy_page_holder.h"
#include "third_party/blink/renderer/core/testing/null_execution_context.h"
#include "third_party/blink/renderer/core/url_pattern/url_pattern.h"
#include "third_party/blink/renderer/platform/scheduler/public/event_loop.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Not;
using ::testing::PrintToString;

// Convenience matcher for list rules that sub-matches on their URLs.
class ListRuleMatcher {
 public:
  explicit ListRuleMatcher(::testing::Matcher<const Vector<KURL>&> url_matcher)
      : url_matcher_(std::move(url_matcher)) {}

  bool MatchAndExplain(const Member<SpeculationRule>& rule,
                       ::testing::MatchResultListener* listener) const {
    return MatchAndExplain(*rule, listener);
  }

  bool MatchAndExplain(const SpeculationRule& rule,
                       ::testing::MatchResultListener* listener) const {
    ::testing::StringMatchResultListener inner_listener;
    const bool matches =
        url_matcher_.MatchAndExplain(rule.urls(), &inner_listener);
    std::string inner_explanation = inner_listener.str();
    if (!inner_explanation.empty())
      *listener << "whose URLs " << inner_explanation;
    return matches;
  }

  void DescribeTo(::std::ostream* os) const {
    *os << "is a list rule whose URLs ";
    url_matcher_.DescribeTo(os);
  }

  void DescribeNegationTo(::std::ostream* os) const {
    *os << "is not list rule whose URLs ";
    url_matcher_.DescribeTo(os);
  }

 private:
  ::testing::Matcher<const Vector<KURL>&> url_matcher_;
};

template <typename... Matchers>
auto MatchesListOfURLs(Matchers&&... matchers) {
  return ::testing::MakePolymorphicMatcher(
      ListRuleMatcher(ElementsAre(std::forward<Matchers>(matchers)...)));
}

MATCHER(RequiresAnonymousClientIPWhenCrossOrigin,
        negation ? "doesn't require anonymous client IP when cross origin"
                 : "requires anonymous client IP when cross origin") {
  return arg->requires_anonymous_client_ip_when_cross_origin();
}

MATCHER(SetsReferrerPolicy,
        std::string(negation ? "doesn't set" : "sets") + " a referrer policy") {
  return arg->referrer_policy().has_value();
}

MATCHER_P(ReferrerPolicyIs,
          policy,
          std::string(negation ? "doesn't have" : "has") + " " +
              PrintToString(policy) + " as the referrer policy") {
  return arg->referrer_policy() == policy;
}

class SpeculationRuleSetTest : public ::testing::Test {
 public:
  SpeculationRuleSetTest()
      : execution_context_(MakeGarbageCollected<NullExecutionContext>()) {}
  ~SpeculationRuleSetTest() override {
    execution_context_->NotifyContextDestroyed();
  }

  SpeculationRuleSet* CreateSpeculationRuleSetWithTargetHint(
      const char* target_hint) {
    return SpeculationRuleSet::Parse(
        String::Format(R"({
        "prefetch": [{
          "source": "list",
          "urls": ["https://example.com/hint.html"],
          "target_hint": "%s"
        }],
        "prefetch_with_subresources": [{
          "source": "list",
          "urls": ["https://example.com/hint.html"],
          "target_hint": "%s"
        }],
        "prerender": [{
          "source": "list",
          "urls": ["https://example.com/hint.html"],
          "target_hint": "%s"
        }]
      })",
                       target_hint, target_hint, target_hint),
        KURL("https://example.com/"), execution_context_);
  }

  ExecutionContext* execution_context() { return execution_context_.Get(); }

 private:
  ScopedSpeculationRulesPrefetchProxyForTest enable_prefetch_{true};
  ScopedPrerender2ForTest enable_prerender2_{true};
  Persistent<ExecutionContext> execution_context_;
};

TEST_F(SpeculationRuleSetTest, Empty) {
  auto* rule_set = SpeculationRuleSet::Parse("{}", KURL("https://example.com/"),
                                             execution_context());
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(rule_set->prefetch_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prefetch_with_subresources_rules(), ElementsAre());
}

TEST_F(SpeculationRuleSetTest, SimplePrefetchRule) {
  auto* rule_set = SpeculationRuleSet::Parse(
      R"({
        "prefetch": [{
          "source": "list",
          "urls": ["https://example.com/index2.html"]
        }]
      })",
      KURL("https://example.com/"), execution_context());
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(
      rule_set->prefetch_rules(),
      ElementsAre(MatchesListOfURLs("https://example.com/index2.html")));
  EXPECT_THAT(rule_set->prefetch_with_subresources_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prerender_rules(), ElementsAre());
}

TEST_F(SpeculationRuleSetTest, SimplePrerenderRule) {
  auto* rule_set = SpeculationRuleSet::Parse(
      R"({
        "prerender": [{
          "source": "list",
          "urls": ["https://example.com/index2.html"]
        }]
      })",
      KURL("https://example.com/"), execution_context());
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(
      rule_set->prerender_rules(),
      ElementsAre(MatchesListOfURLs("https://example.com/index2.html")));
  EXPECT_THAT(rule_set->prefetch_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prefetch_with_subresources_rules(), ElementsAre());
}

TEST_F(SpeculationRuleSetTest, SimplePrefetchWithSubresourcesRule) {
  auto* rule_set = SpeculationRuleSet::Parse(
      R"({
        "prefetch_with_subresources": [{
          "source": "list",
          "urls": ["https://example.com/index2.html"]
        }]
      })",
      KURL("https://example.com/"), execution_context());
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(rule_set->prefetch_rules(), ElementsAre());
  EXPECT_THAT(
      rule_set->prefetch_with_subresources_rules(),
      ElementsAre(MatchesListOfURLs("https://example.com/index2.html")));
  EXPECT_THAT(rule_set->prerender_rules(), ElementsAre());
}

TEST_F(SpeculationRuleSetTest, ResolvesURLs) {
  auto* rule_set = SpeculationRuleSet::Parse(
      R"({
        "prefetch": [{
          "source": "list",
          "urls": [
            "bar",
            "/baz",
            "//example.org/",
            "http://example.net/"
          ]
        }]
      })",
      KURL("https://example.com/foo/"), execution_context());
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(rule_set->prefetch_rules(),
              ElementsAre(MatchesListOfURLs(
                  "https://example.com/foo/bar", "https://example.com/baz",
                  "https://example.org/", "http://example.net/")));
}

TEST_F(SpeculationRuleSetTest, RequiresAnonymousClientIPWhenCrossOrigin) {
  auto* rule_set = SpeculationRuleSet::Parse(
      R"({
        "prefetch": [{
          "source": "list",
          "urls": ["//example.net/anonymous.html"],
          "requires": ["anonymous-client-ip-when-cross-origin"]
        }, {
          "source": "list",
          "urls": ["//example.net/direct.html"]
        }]
      })",
      KURL("https://example.com/"), execution_context());
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(
      rule_set->prefetch_rules(),
      ElementsAre(AllOf(MatchesListOfURLs("https://example.net/anonymous.html"),
                        RequiresAnonymousClientIPWhenCrossOrigin()),
                  AllOf(MatchesListOfURLs("https://example.net/direct.html"),
                        Not(RequiresAnonymousClientIPWhenCrossOrigin()))));
}

TEST_F(SpeculationRuleSetTest, RejectsInvalidJSON) {
  String parse_error;
  auto* rule_set =
      SpeculationRuleSet::Parse("[invalid]", KURL("https://example.com"),
                                execution_context(), &parse_error);
  EXPECT_FALSE(rule_set);
  EXPECT_TRUE(parse_error.Contains("Syntax error"));
}

TEST_F(SpeculationRuleSetTest, RejectsNonObject) {
  String parse_error;
  auto* rule_set = SpeculationRuleSet::Parse("42", KURL("https://example.com"),
                                             execution_context(), &parse_error);
  EXPECT_FALSE(rule_set);
  EXPECT_TRUE(parse_error.Contains("must be an object"));
}

TEST_F(SpeculationRuleSetTest, IgnoresUnknownOrDifferentlyTypedTopLevelKeys) {
  auto* rule_set = SpeculationRuleSet::Parse(
      R"({
        "unrecognized_key": true,
        "prefetch": 42,
        "prefetch_with_subresources": false
      })",
      KURL("https://example.com/"), execution_context());
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(rule_set->prefetch_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prefetch_with_subresources_rules(), ElementsAre());
}

TEST_F(SpeculationRuleSetTest, DropUnrecognizedRules) {
  ScopedSpeculationRulesReferrerPolicyKeyForTest enable_referrer_policy_key{
      true};

  auto* rule_set = SpeculationRuleSet::Parse(
      R"({"prefetch": [)"

      // A rule that doesn't elaborate on its source.
      R"({"urls": ["no-source.html"]},)"

      // A rule with an unrecognized source.
      R"({"source": "magic-8-ball", "urls": ["no-source.html"]},)"

      // A list rule with no "urls" key.
      R"({"source": "list"},)"

      // A list rule where some URL is not a string.
      R"({"source": "list", "urls": [42]},)"

      // A rule with an unrecognized requirement.
      R"({"source": "list", "urls": ["/"], "requires": ["more-vespene-gas"]},)"

      // A rule with a referrer_policy of incorrect type.
      R"({"source": "list", "urls": ["/"], "referrer_policy": 42},)"

      // A rule with an unrecognized referrer_policy.
      R"({"source": "list", "urls": ["/"],
          "referrer_policy": "no-referrrrrrrer"},)"

      // A rule with a legacy value for referrer_policy.
      R"({"source": "list", "urls": ["/"], "referrer_policy": "never"},)"

      // Invalid URLs within a list rule should be discarded.
      // This includes totally invalid ones and ones with unacceptable schemes.
      R"({"source": "list",
          "urls": [
            "valid.html", "mailto:alice@example.com", "http://@:",
            "blob:https://bar"
           ]
         }]})",
      KURL("https://example.com/"), execution_context());
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(rule_set->prefetch_rules(),
              ElementsAre(MatchesListOfURLs("https://example.com/valid.html")));
}

// Test that only prerender rule can process a "_blank" target hint.
TEST_F(SpeculationRuleSetTest, RulesWithTargetHint_Blank) {
  auto* rule_set = CreateSpeculationRuleSetWithTargetHint("_blank");
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(rule_set->prefetch_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prefetch_with_subresources_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prerender_rules(),
              ElementsAre(MatchesListOfURLs("https://example.com/hint.html")));
  EXPECT_EQ(rule_set->prerender_rules()[0]->target_browsing_context_name_hint(),
            mojom::blink::SpeculationTargetHint::kBlank);
}

// Test that only prerender rule can process a "_self" target hint.
TEST_F(SpeculationRuleSetTest, RulesWithTargetHint_Self) {
  auto* rule_set = CreateSpeculationRuleSetWithTargetHint("_self");
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(rule_set->prefetch_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prefetch_with_subresources_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prerender_rules(),
              ElementsAre(MatchesListOfURLs("https://example.com/hint.html")));
  EXPECT_EQ(rule_set->prerender_rules()[0]->target_browsing_context_name_hint(),
            mojom::blink::SpeculationTargetHint::kSelf);
}

// Test that only prerender rule can process a "_parent" target hint but treat
// it as no hint.
// TODO(https://crbug.com/1354049): Support the "_parent" keyword for
// prerendering.
TEST_F(SpeculationRuleSetTest, RulesWithTargetHint_Parent) {
  auto* rule_set = CreateSpeculationRuleSetWithTargetHint("_parent");
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(rule_set->prefetch_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prefetch_with_subresources_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prerender_rules(),
              ElementsAre(MatchesListOfURLs("https://example.com/hint.html")));
  EXPECT_EQ(rule_set->prerender_rules()[0]->target_browsing_context_name_hint(),
            mojom::blink::SpeculationTargetHint::kNoHint);
}

// Test that only prerender rule can process a "_top" target hint but treat it
// as no hint.
// Test that rules with a "_top" hint are ignored.
// TODO(https://crbug.com/1354049): Support the "_top" keyword for prerendering.
TEST_F(SpeculationRuleSetTest, RulesWithTargetHint_Top) {
  auto* rule_set = CreateSpeculationRuleSetWithTargetHint("_top");
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(rule_set->prefetch_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prefetch_with_subresources_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prerender_rules(),
              ElementsAre(MatchesListOfURLs("https://example.com/hint.html")));
  EXPECT_EQ(rule_set->prerender_rules()[0]->target_browsing_context_name_hint(),
            mojom::blink::SpeculationTargetHint::kNoHint);
}

// Test that rules with an empty target hint are ignored.
TEST_F(SpeculationRuleSetTest, RulesWithTargetHint_EmptyString) {
  auto* rule_set = CreateSpeculationRuleSetWithTargetHint("");
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(rule_set->prefetch_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prefetch_with_subresources_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prerender_rules(), ElementsAre());
}

// Test that only prerender rule can process a browsing context name target hint
// but treat it as no hint.
// TODO(https://crbug.com/1354049): Support valid browsing context names.
TEST_F(SpeculationRuleSetTest, RulesWithTargetHint_ValidBrowsingContextName) {
  auto* rule_set = CreateSpeculationRuleSetWithTargetHint("valid");
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(rule_set->prefetch_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prefetch_with_subresources_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prerender_rules(),
              ElementsAre(MatchesListOfURLs("https://example.com/hint.html")));
  EXPECT_EQ(rule_set->prerender_rules()[0]->target_browsing_context_name_hint(),
            mojom::blink::SpeculationTargetHint::kNoHint);
}

// Test that rules with an invalid browsing context name target hint are
// ignored.
TEST_F(SpeculationRuleSetTest, RulesWithTargetHint_InvalidBrowsingContextName) {
  auto* rule_set = CreateSpeculationRuleSetWithTargetHint("_invalid");
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(rule_set->prefetch_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prefetch_with_subresources_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prerender_rules(), ElementsAre());
}

// Test that the the validation of the browsing context keywords runs an ASCII
// case-insensitive match.
TEST_F(SpeculationRuleSetTest, RulesWithTargetHint_CaseInsensitive) {
  auto* rule_set = CreateSpeculationRuleSetWithTargetHint("_BlAnK");
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(rule_set->prefetch_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prefetch_with_subresources_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prerender_rules(),
              ElementsAre(MatchesListOfURLs("https://example.com/hint.html")));
  EXPECT_EQ(rule_set->prerender_rules()[0]->target_browsing_context_name_hint(),
            mojom::blink::SpeculationTargetHint::kBlank);
}

TEST_F(SpeculationRuleSetTest, ReferrerPolicy) {
  ScopedSpeculationRulesReferrerPolicyKeyForTest enable_referrer_policy_key{
      true};

  auto* rule_set = SpeculationRuleSet::Parse(
      R"({
        "prefetch": [{
          "source": "list",
          "urls": ["https://example.com/index2.html"],
          "referrer_policy": "strict-origin"
        }, {
          "source": "list",
          "urls": ["https://example.com/index3.html"]
        }]
      })",
      KURL("https://example.com/"), execution_context());
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(
      rule_set->prefetch_rules(),
      ElementsAre(AllOf(MatchesListOfURLs("https://example.com/index2.html"),
                        ReferrerPolicyIs(
                            network::mojom::ReferrerPolicy::kStrictOrigin)),
                  AllOf(MatchesListOfURLs("https://example.com/index3.html"),
                        Not(SetsReferrerPolicy()))));
}

TEST_F(SpeculationRuleSetTest, PropagatesToDocument) {
  // A <script> with a case-insensitive type match should be propagated to the
  // document.
  // TODO(jbroman): Should we need to enable script? Should that be bypassed?
  DummyPageHolder page_holder;
  page_holder.GetFrame().GetSettings()->SetScriptEnabled(true);
  Document& document = page_holder.GetDocument();
  HTMLScriptElement* script =
      MakeGarbageCollected<HTMLScriptElement>(document, CreateElementFlags());
  script->setAttribute(html_names::kTypeAttr, "SpEcUlAtIoNrUlEs");
  script->setText(
      R"({"prefetch": [
           {"source": "list", "urls": ["https://example.com/foo"]}
         ],
         "prerender": [
           {"source": "list", "urls": ["https://example.com/bar"]}
         ]
         })");
  document.head()->appendChild(script);

  auto* supplement = DocumentSpeculationRules::FromIfExists(document);
  ASSERT_TRUE(supplement);
  ASSERT_EQ(supplement->rule_sets().size(), 1u);
  SpeculationRuleSet* rule_set = supplement->rule_sets()[0];
  EXPECT_THAT(rule_set->prefetch_rules(),
              ElementsAre(MatchesListOfURLs("https://example.com/foo")));
  EXPECT_THAT(rule_set->prerender_rules(),
              ElementsAre(MatchesListOfURLs("https://example.com/bar")));
}

HTMLScriptElement* InsertSpeculationRules(Document& document,
                                          const String& speculation_script) {
  HTMLScriptElement* script =
      MakeGarbageCollected<HTMLScriptElement>(document, CreateElementFlags());
  script->setAttribute(html_names::kTypeAttr, "SpEcUlAtIoNrUlEs");
  script->setText(speculation_script);
  document.head()->appendChild(script);
  return script;
}

// This runs the functor while observing any speculation rules sent by it.
// At least one update is expected.
template <typename F>
void PropagateRulesToStubSpeculationHost(DummyPageHolder& page_holder,
                                         StubSpeculationHost& speculation_host,
                                         const F& functor) {
  // A <script> with a case-insensitive type match should be propagated to the
  // browser via Mojo.
  // TODO(jbroman): Should we need to enable script? Should that be bypassed?
  LocalFrame& frame = page_holder.GetFrame();
  frame.GetSettings()->SetScriptEnabled(true);

  auto& broker = frame.DomWindow()->GetBrowserInterfaceBroker();
  broker.SetBinderForTesting(
      mojom::blink::SpeculationHost::Name_,
      WTF::BindRepeating(&StubSpeculationHost::BindUnsafe,
                         WTF::Unretained(&speculation_host)));

  base::RunLoop run_loop;
  speculation_host.SetDoneClosure(run_loop.QuitClosure());
  functor();
  run_loop.Run();

  broker.SetBinderForTesting(mojom::blink::SpeculationHost::Name_, {});
}

// This function adds a speculationrules script to the given page, and simulates
// the process of sending the parsed candidates to the browser.
void PropagateRulesToStubSpeculationHost(DummyPageHolder& page_holder,
                                         StubSpeculationHost& speculation_host,
                                         const String& speculation_script) {
  PropagateRulesToStubSpeculationHost(page_holder, speculation_host, [&]() {
    InsertSpeculationRules(page_holder.GetDocument(), speculation_script);
  });
}

TEST_F(SpeculationRuleSetTest, PropagatesAllRulesToBrowser) {
  DummyPageHolder page_holder;
  StubSpeculationHost speculation_host;
  const String speculation_script =
      R"({"prefetch": [
           {"source": "list",
            "urls": ["https://example.com/foo", "https://example.com/bar"],
            "requires": ["anonymous-client-ip-when-cross-origin"]}
         ],
          "prerender": [
           {"source": "list", "urls": ["https://example.com/prerender"]}
         ]
         })";
  PropagateRulesToStubSpeculationHost(page_holder, speculation_host,
                                      speculation_script);

  const auto& candidates = speculation_host.candidates();
  ASSERT_EQ(candidates.size(), 3u);
  {
    const auto& candidate = candidates[0];
    EXPECT_EQ(candidate->action, mojom::blink::SpeculationAction::kPrefetch);
    EXPECT_EQ(candidate->url, "https://example.com/foo");
    EXPECT_TRUE(candidate->requires_anonymous_client_ip_when_cross_origin);
  }
  {
    const auto& candidate = candidates[1];
    EXPECT_EQ(candidate->action, mojom::blink::SpeculationAction::kPrefetch);
    EXPECT_EQ(candidate->url, "https://example.com/bar");
    EXPECT_TRUE(candidate->requires_anonymous_client_ip_when_cross_origin);
  }
  {
    const auto& candidate = candidates[2];
    EXPECT_EQ(candidate->action, mojom::blink::SpeculationAction::kPrerender);
    EXPECT_EQ(candidate->url, "https://example.com/prerender");
  }
}

// Tests that prefetch rules are ignored unless SpeculationRulesPrefetchProxy
// is enabled.
TEST_F(SpeculationRuleSetTest, PrerenderIgnorePrefetchRules) {
  // Overwrite the kSpeculationRulesPrefetchProxy flag.
  ScopedSpeculationRulesPrefetchProxyForTest enable_prefetch{false};

  DummyPageHolder page_holder;
  StubSpeculationHost speculation_host;
  const String speculation_script =
      R"({"prefetch_with_subresources": [
           {"source": "list",
            "urls": ["https://example.com/foo", "https://example.com/bar"],
            "requires": ["anonymous-client-ip-when-cross-origin"]}
         ],
          "prerender": [
           {"source": "list", "urls": ["https://example.com/prerender"]}
         ]
         })";
  PropagateRulesToStubSpeculationHost(page_holder, speculation_host,
                                      speculation_script);

  const auto& candidates = speculation_host.candidates();
  EXPECT_EQ(candidates.size(), 1u);
  EXPECT_FALSE(base::ranges::any_of(candidates, [](const auto& candidate) {
    return candidate->action ==
           mojom::blink::SpeculationAction::kPrefetchWithSubresources;
  }));
}

// Tests that prerender rules are ignored unless Prerender2 is enabled.
TEST_F(SpeculationRuleSetTest, PrefetchIgnorePrerenderRules) {
  // Overwrite the kPrerender2 flag.
  ScopedPrerender2ForTest enable_prerender{false};

  DummyPageHolder page_holder;
  StubSpeculationHost speculation_host;
  const String speculation_script =
      R"({"prefetch": [
           {"source": "list",
            "urls": ["https://example.com/foo", "https://example.com/bar"],
            "requires": ["anonymous-client-ip-when-cross-origin"]}
         ],
          "prerender": [
           {"source": "list", "urls": ["https://example.com/prerender"]}
         ]
         })";
  PropagateRulesToStubSpeculationHost(page_holder, speculation_host,
                                      speculation_script);

  const auto& candidates = speculation_host.candidates();
  EXPECT_EQ(candidates.size(), 2u);
  EXPECT_FALSE(base::ranges::any_of(candidates, [](const auto& candidate) {
    return candidate->action == mojom::blink::SpeculationAction::kPrerender;
  }));
}

// Tests that the presence of a speculationrules script is recorded.
TEST_F(SpeculationRuleSetTest, UseCounter) {
  DummyPageHolder page_holder;
  StubSpeculationHost speculation_host;
  page_holder.GetFrame().GetSettings()->SetScriptEnabled(true);
  EXPECT_FALSE(
      page_holder.GetDocument().IsUseCounted(WebFeature::kSpeculationRules));

  const String speculation_script =
      R"({"prefetch": [{"source": "list", "urls": ["/foo"]}]})";
  PropagateRulesToStubSpeculationHost(page_holder, speculation_host,
                                      speculation_script);
  EXPECT_TRUE(
      page_holder.GetDocument().IsUseCounted(WebFeature::kSpeculationRules));
}

// Tests that rules removed before the task to update speculation candidates
// runs are not reported.
TEST_F(SpeculationRuleSetTest, AddAndRemoveInSameTask) {
  DummyPageHolder page_holder;
  StubSpeculationHost speculation_host;

  PropagateRulesToStubSpeculationHost(page_holder, speculation_host, [&]() {
    InsertSpeculationRules(page_holder.GetDocument(),
                           R"({"prefetch": [
             {"source": "list", "urls": ["https://example.com/foo"]}]})");
    HTMLScriptElement* to_remove =
        InsertSpeculationRules(page_holder.GetDocument(),
                               R"({"prefetch": [
             {"source": "list", "urls": ["https://example.com/bar"]}]})");
    InsertSpeculationRules(page_holder.GetDocument(),
                           R"({"prefetch": [
             {"source": "list", "urls": ["https://example.com/baz"]}]})");
    to_remove->remove();
  });

  const auto& candidates = speculation_host.candidates();
  ASSERT_EQ(candidates.size(), 2u);
  EXPECT_EQ(candidates[0]->url, "https://example.com/foo");
  EXPECT_EQ(candidates[1]->url, "https://example.com/baz");
}

// Tests that rules removed after being previously reported are reported as
// removed.
TEST_F(SpeculationRuleSetTest, AddAndRemoveAfterReport) {
  DummyPageHolder page_holder;
  StubSpeculationHost speculation_host;

  HTMLScriptElement* to_remove = nullptr;
  PropagateRulesToStubSpeculationHost(page_holder, speculation_host, [&]() {
    InsertSpeculationRules(page_holder.GetDocument(),
                           R"({"prefetch": [
             {"source": "list", "urls": ["https://example.com/foo"]}]})");
    to_remove = InsertSpeculationRules(page_holder.GetDocument(),
                                       R"({"prefetch": [
             {"source": "list", "urls": ["https://example.com/bar"]}]})");
    InsertSpeculationRules(page_holder.GetDocument(),
                           R"({"prefetch": [
             {"source": "list", "urls": ["https://example.com/baz"]}]})");
  });

  {
    const auto& candidates = speculation_host.candidates();
    ASSERT_EQ(candidates.size(), 3u);
    EXPECT_EQ(candidates[0]->url, "https://example.com/foo");
    EXPECT_EQ(candidates[1]->url, "https://example.com/bar");
    EXPECT_EQ(candidates[2]->url, "https://example.com/baz");
  }

  PropagateRulesToStubSpeculationHost(page_holder, speculation_host,
                                      [&]() { to_remove->remove(); });

  {
    const auto& candidates = speculation_host.candidates();
    ASSERT_EQ(candidates.size(), 2u);
    EXPECT_EQ(candidates[0]->url, "https://example.com/foo");
    EXPECT_EQ(candidates[1]->url, "https://example.com/baz");
  }
}

// Tests that removed candidates are reported in a microtask.
// This is somewhat difficult to observe in practice, but most sharply visible
// if a removal occurs and then in a subsequent microtask an addition occurs.
TEST_F(SpeculationRuleSetTest, RemoveInMicrotask) {
  DummyPageHolder page_holder;
  StubSpeculationHost speculation_host;

  base::RunLoop run_loop;
  base::MockCallback<base::RepeatingCallback<void(
      const Vector<mojom::blink::SpeculationCandidatePtr>&)>>
      mock_callback;
  {
    ::testing::InSequence sequence;
    EXPECT_CALL(mock_callback, Run(::testing::SizeIs(2)));
    EXPECT_CALL(mock_callback, Run(::testing::SizeIs(1)));
    EXPECT_CALL(mock_callback, Run(::testing::SizeIs(2)))
        .WillOnce(::testing::Invoke([&]() { run_loop.Quit(); }));
  }
  speculation_host.SetCandidatesUpdatedCallback(mock_callback.Get());

  LocalFrame& frame = page_holder.GetFrame();
  frame.GetSettings()->SetScriptEnabled(true);
  auto& broker = frame.DomWindow()->GetBrowserInterfaceBroker();
  broker.SetBinderForTesting(
      mojom::blink::SpeculationHost::Name_,
      WTF::BindRepeating(&StubSpeculationHost::BindUnsafe,
                         WTF::Unretained(&speculation_host)));

  // First simulated task adds the rule sets.
  InsertSpeculationRules(page_holder.GetDocument(),
                         R"({"prefetch": [
           {"source": "list", "urls": ["https://example.com/foo"]}]})");
  HTMLScriptElement* to_remove =
      InsertSpeculationRules(page_holder.GetDocument(),
                             R"({"prefetch": [
             {"source": "list", "urls": ["https://example.com/bar"]}]})");
  scoped_refptr<scheduler::EventLoop> event_loop =
      frame.DomWindow()->GetAgent()->event_loop();
  event_loop->PerformMicrotaskCheckpoint();

  // Second simulated task removes the rule sets, then adds another one in a
  // microtask which is queued later than any queued during the removal.
  to_remove->remove();
  event_loop->EnqueueMicrotask(base::BindLambdaForTesting([&] {
    InsertSpeculationRules(page_holder.GetDocument(),
                           R"({"prefetch": [
           {"source": "list", "urls": ["https://example.com/baz"]}]})");
  }));
  event_loop->PerformMicrotaskCheckpoint();

  run_loop.Run();
  broker.SetBinderForTesting(mojom::blink::SpeculationHost::Name_, {});
}

class ConsoleCapturingChromeClient : public EmptyChromeClient {
 public:
  void AddMessageToConsole(LocalFrame*,
                           mojom::ConsoleMessageSource,
                           mojom::ConsoleMessageLevel,
                           const String& message,
                           unsigned line_number,
                           const String& source_id,
                           const String& stack_trace) override {
    messages_.push_back(message);
  }

  const Vector<String>& ConsoleMessages() const { return messages_; }

 private:
  Vector<String> messages_;
};

// Tests that parse errors are logged to the console.
TEST_F(SpeculationRuleSetTest, ConsoleWarning) {
  auto* chrome_client = MakeGarbageCollected<ConsoleCapturingChromeClient>();
  DummyPageHolder page_holder(/*initial_view_size=*/{}, chrome_client);
  page_holder.GetFrame().GetSettings()->SetScriptEnabled(true);

  Document& document = page_holder.GetDocument();
  HTMLScriptElement* script =
      MakeGarbageCollected<HTMLScriptElement>(document, CreateElementFlags());
  script->setAttribute(html_names::kTypeAttr, "speculationrules");
  script->setText("[invalid]");
  document.head()->appendChild(script);

  EXPECT_TRUE(base::ranges::any_of(
      chrome_client->ConsoleMessages(),
      [](const String& message) { return message.Contains("Syntax error"); }));
}

TEST_F(SpeculationRuleSetTest, RejectsWhereClause) {
  auto* rule_set = SpeculationRuleSet::Parse(
      R"({
        "prefetch": [{
          "source": "document",
          "where": {}
        }]
      })",
      KURL("https://example.com/"), execution_context());
  EXPECT_THAT(rule_set->prefetch_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prerender_rules(), ElementsAre());
  EXPECT_THAT(rule_set->prefetch_with_subresources_rules(), ElementsAre());
}

MATCHER_P(MatchesPredicate,
          matcher,
          ::testing::DescribeMatcher<DocumentRulePredicate>(matcher)) {
  if (!arg->predicate()) {
    *result_listener << "does not have a predicate";
    return false;
  }
  return ExplainMatchResult(matcher, *(arg->predicate()), result_listener);
}

String GetTypeString(DocumentRulePredicate::Type type) {
  switch (type) {
    case DocumentRulePredicate::Type::kAnd:
      return "And";
    case DocumentRulePredicate::Type::kOr:
      return "Or";
    case DocumentRulePredicate::Type::kNot:
      return "Not";
    case DocumentRulePredicate::Type::kURLPatterns:
      return "Href";
  }
}

std::string GetMatcherDescription(
    const ::testing::Matcher<DocumentRulePredicate>& matcher) {
  std::stringstream ss;
  matcher.DescribeTo(&ss);
  return ss.str();
}

class ConditionMatcher {
 public:
  explicit ConditionMatcher(
      DocumentRulePredicate::Type type,
      Vector<::testing::Matcher<DocumentRulePredicate>> matchers)
      : type_(type), matchers_(std::move(matchers)) {}

  bool MatchAndExplain(DocumentRulePredicate* predicate,
                       ::testing::MatchResultListener* listener) const {
    if (!predicate)
      return false;
    return MatchAndExplain(*predicate, listener);
  }

  bool MatchAndExplain(const DocumentRulePredicate& predicate,
                       ::testing::MatchResultListener* listener) const {
    ::testing::StringMatchResultListener inner_listener;
    const auto& predicates = predicate.GetSubPredicatesForTesting();
    if (predicate.GetTypeForTesting() != type_ ||
        predicates.size() != matchers_.size()) {
      *listener << predicate.ToString();
      return false;
    }

    bool matches = true;
    for (wtf_size_t i = 0; i < matchers_.size(); i++) {
      if (!matchers_[i].MatchAndExplain(*(predicates[i]), &inner_listener)) {
        matches = false;
        break;
      }
    }
    *listener << predicate.ToString();
    return matches;
  }

  void DescribeTo(::std::ostream* os) const {
    *os << GetTypeString(type_) << "(";
    for (wtf_size_t i = 0; i < matchers_.size(); i++) {
      *os << GetMatcherDescription(matchers_[i]);
      if (i != matchers_.size() - 1)
        *os << ", ";
    }
    *os << ")";
  }

  void DescribeNegationTo(::std::ostream* os) const { DescribeTo(os); }

 private:
  DocumentRulePredicate::Type type_;
  Vector<::testing::Matcher<DocumentRulePredicate>> matchers_;
};

auto And(Vector<::testing::Matcher<DocumentRulePredicate>> matchers = {}) {
  return testing::MakePolymorphicMatcher(
      ConditionMatcher(DocumentRulePredicate::Type::kAnd, std::move(matchers)));
}

auto Or(Vector<::testing::Matcher<DocumentRulePredicate>> matchers = {}) {
  return testing::MakePolymorphicMatcher(
      ConditionMatcher(DocumentRulePredicate::Type::kOr, std::move(matchers)));
}

auto Neg(::testing::Matcher<DocumentRulePredicate> matcher) {
  return testing::MakePolymorphicMatcher(
      ConditionMatcher(DocumentRulePredicate::Type::kNot, {matcher}));
}

class HrefMatcher {
 public:
  explicit HrefMatcher(Vector<::testing::Matcher<URLPattern>> pattern_matchers)
      : pattern_matchers_(std::move(pattern_matchers)) {}

  bool MatchAndExplain(DocumentRulePredicate* predicate,
                       ::testing::MatchResultListener* listener) const {
    if (!predicate)
      return false;
    return MatchAndExplain(*predicate, listener);
  }

  bool MatchAndExplain(const DocumentRulePredicate& predicate,
                       ::testing::MatchResultListener* listener) const {
    if (predicate.GetTypeForTesting() !=
            DocumentRulePredicate::Type::kURLPatterns ||
        predicate.GetURLPatternsForTesting().size() !=
            pattern_matchers_.size()) {
      *listener << predicate.ToString();
      return false;
    }

    auto patterns = predicate.GetURLPatternsForTesting();
    ::testing::StringMatchResultListener inner_listener;
    for (wtf_size_t i = 0; i < pattern_matchers_.size(); i++) {
      if (!pattern_matchers_[i].MatchAndExplain(*patterns[i],
                                                &inner_listener)) {
        *listener << predicate.ToString();
        return false;
      }
    }
    return true;
  }

  void DescribeTo(::std::ostream* os) const {
    *os << GetTypeString(DocumentRulePredicate::Type::kURLPatterns) << "([";
    for (wtf_size_t i = 0; i < pattern_matchers_.size(); i++) {
      pattern_matchers_[i].DescribeTo(os);
      if (i != pattern_matchers_.size() - 1)
        *os << ", ";
    }
    *os << "])";
  }

  void DescribeNegationTo(::std::ostream* os) const { DescribeTo(os); }

 private:
  Vector<::testing::Matcher<URLPattern>> pattern_matchers_;
};

auto Href(Vector<::testing::Matcher<URLPattern>> pattern_matchers = {}) {
  return testing::MakePolymorphicMatcher(HrefMatcher(pattern_matchers));
}

class URLPatternMatcher {
 public:
  explicit URLPatternMatcher(String pattern, const KURL& base_url) {
    auto* url_pattern_input = MakeGarbageCollected<V8URLPatternInput>(pattern);
    url_pattern_ =
        URLPattern::Create(url_pattern_input, base_url, ASSERT_NO_EXCEPTION);
  }

  bool MatchAndExplain(URLPattern* pattern,
                       ::testing::MatchResultListener* listener) const {
    if (!pattern)
      return false;
    return MatchAndExplain(*pattern, listener);
  }

  bool MatchAndExplain(const URLPattern& pattern,
                       ::testing::MatchResultListener* listener) const {
    using Component = V8URLPatternComponent::Enum;
    Component components[] = {Component::kProtocol, Component::kUsername,
                              Component::kPassword, Component::kHostname,
                              Component::kPort,     Component::kPathname,
                              Component::kSearch,   Component::kHash};
    for (auto component : components) {
      if (URLPattern::compareComponent(V8URLPatternComponent(component),
                                       url_pattern_, &pattern) != 0) {
        return false;
      }
    }
    return true;
  }

  void DescribeTo(::std::ostream* os) const { *os << url_pattern_->ToString(); }

  void DescribeNegationTo(::std::ostream* os) const { DescribeTo(os); }

 private:
  Persistent<URLPattern> url_pattern_;
};

auto URLPattern(String pattern,
                const KURL& base_url = KURL("https://example.com/")) {
  return ::testing::MakePolymorphicMatcher(
      URLPatternMatcher(pattern, base_url));
}

class DocumentRulesTest : public SpeculationRuleSetTest {
 public:
  ~DocumentRulesTest() override = default;

  DocumentRulePredicate* CreatePredicate(
      String where_text,
      KURL base_url = KURL("https://example.com/")) {
    auto* rule_set =
        SpeculationRuleSet::Parse(String::Format(R"({
        "prefetch": [{
          "source": "document",
          "where": {%s}
        }]
      })",
                                                 where_text.Latin1().c_str()),
                                  base_url, execution_context());
    DCHECK(!rule_set->prefetch_rules().empty()) << "Invalid predicate.";
    return rule_set->prefetch_rules()[0]->predicate();
  }

 private:
  ScopedSpeculationRulesDocumentRulesForTest enable_document_rules_{true};
};

TEST_F(DocumentRulesTest, ParseAnd) {
  auto* rule_set = SpeculationRuleSet::Parse(
      R"({
        "prefetch": [{
          "source": "document",
          "where": { "and": [] }
        }, {
          "source": "document",
          "where": {"and": [{"and": []}, {"and": []}]}
        }]
      })",
      KURL("https://example.com/"), execution_context());
  EXPECT_THAT(rule_set->prefetch_rules(),
              ElementsAre(MatchesPredicate(And()),
                          MatchesPredicate(And({And(), And()}))));
}

TEST_F(DocumentRulesTest, ParseOr) {
  auto* rule_set = SpeculationRuleSet::Parse(
      R"({
        "prefetch": [{
          "source": "document",
          "where": { "or": [] }
        }, {
          "source": "document",
          "where": {"or": [{"and": []}, {"or": []}]}
        }]
      })",
      KURL("https://example.com/"), execution_context());
  EXPECT_THAT(
      rule_set->prefetch_rules(),
      ElementsAre(MatchesPredicate(Or()), MatchesPredicate(Or({And(), Or()}))));
}

TEST_F(DocumentRulesTest, ParseNot) {
  auto* rule_set = SpeculationRuleSet::Parse(
      R"({
        "prefetch": [{
          "source": "document",
          "where": {"not": {"and": []}}
        }, {
          "source": "document",
          "where": {"not": {"or": [{"and": []}, {"or": []}]}}
        }]
      })",
      KURL("https://example.com/"), execution_context());
  EXPECT_THAT(rule_set->prefetch_rules(),
              ElementsAre(MatchesPredicate(Neg(And())),
                          MatchesPredicate(Neg(Or({And(), Or()})))));
}

TEST_F(DocumentRulesTest, ParseHref) {
  auto* rule_set = SpeculationRuleSet::Parse(
      R"({
        "prefetch": [{
          "source": "document",
          "where": {"href_matches": "/foo#bar"}
        }, {
          "source": "document",
          "where": {"href_matches": {"pathname": "/foo"}}
        }, {
          "source": "document",
          "where": {"href_matches": [
            {"pathname": "/buzz"},
            "/fizz",
            {"hostname": "bar.com"}
          ]}
        }, {
          "source": "document",
          "where": {"or": [
            {"href_matches": {"hostname": "foo.com"}},
            {"not": {"href_matches": {"protocol": "http", "hostname": "*"}}}
          ]}
        }]
      })",
      KURL("https://example.com/"), execution_context());
  EXPECT_THAT(
      rule_set->prefetch_rules(),
      ElementsAre(
          MatchesPredicate(Href({URLPattern("/foo#bar")})),
          MatchesPredicate(Href({URLPattern("/foo")})),
          MatchesPredicate(Href({URLPattern("/buzz"), URLPattern("/fizz"),
                                 URLPattern("https://bar.com")})),
          MatchesPredicate(Or({Href({URLPattern("https://foo.com")}),
                               Neg(Href({URLPattern("http://*")}))}))));
}

TEST_F(DocumentRulesTest, ParseHref_AllUrlPatternKeys) {
  auto* href_matches = CreatePredicate(R"("href_matches": {
    "username": "",
    "password": "",
    "port": "*",
    "pathname": "/*",
    "search": "*",
    "hash": "",
    "protocol": "https",
    "hostname": "abc.xyz",
    "baseURL": "https://example.com"
  })");
  EXPECT_THAT(href_matches, Href({URLPattern("https://abc.xyz:*/*\\?*")}));
}

TEST_F(DocumentRulesTest, HrefMatchesWithBaseURL) {
  auto* without_base_specified = CreatePredicate(
      R"("href_matches": {"pathname": "/hello"})", KURL("http://foo.com"));
  EXPECT_THAT(without_base_specified,
              Href({URLPattern("http://foo.com/hello")}));
  auto* with_base_specified = CreatePredicate(
      R"("href_matches": {"pathname": "hello", "baseURL": "http://bar.com"})",
      KURL("http://foo.com"));
  EXPECT_THAT(with_base_specified, Href({URLPattern("http://bar.com/hello")}));
}

TEST_F(DocumentRulesTest, DropInvalidRules) {
  auto* rule_set = SpeculationRuleSet::Parse(
      R"({"prefetch": [)"

      // A rule that doesn't elaborate on its source.
      R"({"where": {"and": []}},)"

      // A rule with an unrecognized source.
      R"({"source": "magic-8-ball", "where": {"and": []}},)"

      // A list rule with a "where" key.
      R"({"source": "list", "where": {"and": []}},)"

      // A document rule with a "urls" key.
      R"({"source": "document", "urls": ["foo.html"]},)"

      // "where" clause is not a map.
      R"({"source": "document", "where": [{"and": []}]},)"

      // "where" clause does not contain one of "and", "or", "not",
      // "href_matches" and "selector_matches"
      R"({"source": "document", "where": {"foo": "bar"}},)"

      // "where" clause has both "and" and "or" as keys
      R"({"source": "document", "where": {"and": [], "or": []}},)"

      // "and" key has object value.
      R"({"source": "document", "where": {"and": {}}},)"

      // "or" key has object value.
      R"({"source": "document", "where": {"or": {}}},)"

      // "and" key has invalid list value.
      R"({"source": "document", "where": {"and": ["foo"]}},)"

      // "not" key has list value.
      R"({"source": "document", "where": {"not": [{"and": []}]}},)"

      // "not" key has empty object value.
      R"({"source": "document", "where": {"not": {}}},)"

      // "not" key has invalid object value.
      R"({"source": "document", "where": {"not": {"foo": "bar"}}},)"

      // pattern is not a string or map value.
      R"({"source": "document", "where": {"href_matches": false}},)"

      // pattern string is invalid.
      R"({"source": "document", "where": {"href_matches": "::"}},)"

      // pattern object has invalid key.
      R"({"source": "document", "where": {"href_matches": {"foo": "bar"}}},)"

      // pattern object has invalid value.
      R"({"source": "document",
          "where": {"href_matches": {"protocol": "::"}}},)"

      // valid document rule.
      R"({"source": "document",
          "where": {"and": [
            {"or": [{"href_matches": "/hello.html"}]},
            {"not": {"and": [{"href_matches": {"hostname": "world.com"}}]}}
          ]}
         }]})",
      KURL("https://example.com/"), execution_context());
  ASSERT_TRUE(rule_set);
  EXPECT_THAT(rule_set->prefetch_rules(),
              ElementsAre(MatchesPredicate(
                  And({Or({Href({URLPattern("/hello.html")})}),
                       Neg(And({Href({URLPattern("https://world.com")})}))}))));
}

TEST_F(DocumentRulesTest, DefaultPredicate) {
  auto* rule_set = SpeculationRuleSet::Parse(
      R"({
        "prefetch": [{
          "source": "document"
        }]
      })",
      KURL("https://example.com/"), execution_context());
  EXPECT_THAT(rule_set->prefetch_rules(), ElementsAre(MatchesPredicate(And())));
}

TEST_F(DocumentRulesTest, EvaluateCombinators) {
  DummyPageHolder page_holder;
  Document& document = page_holder.GetDocument();
  Element* link = MakeGarbageCollected<HTMLAnchorElement>(document);

  auto* empty_and = CreatePredicate(R"("and": [])");
  EXPECT_THAT(empty_and, And());
  EXPECT_TRUE(empty_and->Matches(*link));

  auto* empty_or = CreatePredicate(R"("or": [])");
  EXPECT_THAT(empty_or, Or());
  EXPECT_FALSE(empty_or->Matches(*link));

  auto* and_false_false_false =
      CreatePredicate(R"("and": [{"or": []}, {"or": []}, {"or": []}])");
  EXPECT_THAT(and_false_false_false, And({Or(), Or(), Or()}));
  EXPECT_FALSE(and_false_false_false->Matches(*link));

  auto* and_false_true_false =
      CreatePredicate(R"("and": [{"or": []}, {"and": []}, {"or": []}])");
  EXPECT_THAT(and_false_true_false, And({Or(), And(), Or()}));
  EXPECT_FALSE(and_false_true_false->Matches(*link));

  auto* and_true_true_true =
      CreatePredicate(R"("and": [{"and": []}, {"and": []}, {"and": []}])");
  EXPECT_THAT(and_true_true_true, And({And(), And(), And()}));
  EXPECT_TRUE(and_true_true_true->Matches(*link));

  auto* or_false_false_false =
      CreatePredicate(R"("or": [{"or": []}, {"or": []}, {"or": []}])");
  EXPECT_THAT(or_false_false_false, Or({Or(), Or(), Or()}));
  EXPECT_FALSE(or_false_false_false->Matches(*link));

  auto* or_false_true_false =
      CreatePredicate(R"("or": [{"or": []}, {"and": []}, {"or": []}])");
  EXPECT_THAT(or_false_true_false, Or({Or(), And(), Or()}));
  EXPECT_TRUE(or_false_true_false->Matches(*link));

  auto* or_true_true_true =
      CreatePredicate(R"("or": [{"and": []}, {"and": []}, {"and": []}])");
  EXPECT_THAT(or_true_true_true, Or({And(), And(), And()}));
  EXPECT_TRUE(or_true_true_true->Matches(*link));

  auto* not_true = CreatePredicate(R"("not": {"and": []})");
  EXPECT_THAT(not_true, Neg(And()));
  EXPECT_FALSE(not_true->Matches(*link));

  auto* not_false = CreatePredicate(R"("not": {"or": []})");
  EXPECT_THAT(not_false, Neg(Or()));
  EXPECT_TRUE(not_false->Matches(*link));
}

TEST_F(DocumentRulesTest, EvaluateHrefMatches) {
  DummyPageHolder page_holder;
  Document& document = page_holder.GetDocument();
  HTMLAnchorElement* link = MakeGarbageCollected<HTMLAnchorElement>(document);
  link->setHref("https://foo.com/bar.html?fizz=buzz");

  // No patterns specified, will not match any link.
  auto* empty = CreatePredicate(R"("href_matches": [])");
  EXPECT_FALSE(empty->Matches(*link));

  // Single pattern (should match).
  auto* single =
      CreatePredicate(R"("href_matches": "https://foo.com/bar.html?*")");
  EXPECT_TRUE(single->Matches(*link));

  // Two patterns which don't match.
  auto* double_fail = CreatePredicate(
      R"("href_matches": ["http://foo.com/*", "https://bar.com/*"])");
  EXPECT_FALSE(double_fail->Matches(*link));

  // One pattern that matches, one that doesn't - should still pass due to
  // an implicit or between patterns in a href_matches list.
  auto* pass_fail = CreatePredicate(
      R"("href_matches": ["https://foo.com/bar.html?*", "https://bar.com/*"])");
  EXPECT_TRUE(pass_fail->Matches(*link));
}

}  // namespace
}  // namespace blink
