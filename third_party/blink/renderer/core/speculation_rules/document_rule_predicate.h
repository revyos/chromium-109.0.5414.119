// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_SPECULATION_RULES_DOCUMENT_RULE_PREDICATE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_SPECULATION_RULES_DOCUMENT_RULE_PREDICATE_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_vector.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

class Element;
class ExceptionState;
class JSONObject;
class KURL;
class URLPattern;

class CORE_EXPORT DocumentRulePredicate
    : public GarbageCollected<DocumentRulePredicate> {
 public:
  DocumentRulePredicate() = default;
  virtual ~DocumentRulePredicate() = default;

  static DocumentRulePredicate* Parse(JSONObject* input,
                                      const KURL& base_url,
                                      ExceptionState& exception_state);
  // Creates a predicate that matches with any link (i.e. Matches() below will
  // always returns true).
  static DocumentRulePredicate* MakeDefaultPredicate();

  virtual bool Matches(const Element& link) const = 0;

  // Methods for testing.
  enum class Type { kAnd, kOr, kNot, kURLPatterns };
  virtual String ToString() const = 0;
  virtual Type GetTypeForTesting() const = 0;
  virtual HeapVector<Member<DocumentRulePredicate>> GetSubPredicatesForTesting()
      const;
  virtual HeapVector<Member<URLPattern>> GetURLPatternsForTesting() const;

  virtual void Trace(Visitor*) const;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_SPECULATION_RULES_DOCUMENT_RULE_PREDICATE_H_
