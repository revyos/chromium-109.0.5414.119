// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_STYLE_SCOPED_CSS_NAME_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_STYLE_SCOPED_CSS_NAME_H_

#include "base/memory/values_equivalent.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/wtf/hash_functions.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string_hash.h"

namespace blink {

class TreeScope;

// Stores a CSS name as an AtomicString along with a TreeScope to support
// tree-scoped names and references for e.g. anchor-name. If the TreeScope
// pointer is null, we do not support such references, for instance for UA
// stylesheets.
class CORE_EXPORT ScopedCSSName : public GarbageCollected<ScopedCSSName> {
 public:
  ScopedCSSName(const AtomicString& name, const TreeScope* tree_scope)
      : name_(name), tree_scope_(tree_scope) {
    DCHECK(name);
  }

  const AtomicString& GetName() const { return name_; }
  const TreeScope* GetTreeScope() const { return tree_scope_; }

  bool operator==(const ScopedCSSName& other) const {
    return name_ == other.name_ && tree_scope_ == other.tree_scope_;
  }
  bool operator!=(const ScopedCSSName& other) const {
    return !operator==(other);
  }

  unsigned GetHash() const {
    unsigned hash = WTF::AtomicStringHash::GetHash(name_);
    WTF::AddIntToHash(
        hash, WTF::PtrHash<const TreeScope>::GetHash(tree_scope_.Get()));
    return hash;
  }

  void Trace(Visitor* visitor) const;

 private:
  AtomicString name_;

  // Weak reference to break ref cycle with both GC-ed and ref-counted objects:
  // Document -> ComputedStyle -> ScopedCSSName -> TreeScope(Document)
  WeakMember<const TreeScope> tree_scope_;
};

}  // namespace blink

namespace WTF {

// Allows creating a hash table of ScopedCSSName in wrapper pointers (e.g.,
// HeapHashSet<Member<ScopedCSSName>>) that hashes the ScopedCSSNames directly
// instead of the wrapper pointers.

template <typename ScopedCSSNameWrapperPtr>
struct ScopedCSSNameWrapperPtrHash {
  STATIC_ONLY(ScopedCSSNameWrapperPtrHash);
  static unsigned GetHash(const ScopedCSSNameWrapperPtr& name) {
    return name->GetHash();
  }
  static bool Equal(const ScopedCSSNameWrapperPtr& a,
                    const ScopedCSSNameWrapperPtr& b) {
    return base::ValuesEquivalent(a, b);
  }
  static const bool safe_to_compare_to_empty_or_deleted = true;
};

template <>
struct DefaultHash<blink::Member<blink::ScopedCSSName>>
    : ScopedCSSNameWrapperPtrHash<blink::Member<blink::ScopedCSSName>> {};
template <>
struct DefaultHash<blink::Member<const blink::ScopedCSSName>>
    : ScopedCSSNameWrapperPtrHash<blink::Member<const blink::ScopedCSSName>> {};

}  // namespace WTF

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_STYLE_SCOPED_CSS_NAME_H_
