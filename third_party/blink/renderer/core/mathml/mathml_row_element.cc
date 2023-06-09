// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/mathml/mathml_row_element.h"

#include "third_party/blink/renderer/core/dom/element_traversal.h"
#include "third_party/blink/renderer/core/layout/ng/mathml/layout_ng_mathml_block.h"
#include "third_party/blink/renderer/core/mathml/mathml_operator_element.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"

namespace blink {

MathMLRowElement::MathMLRowElement(const QualifiedName& tagName,
                                   Document& document)
    : MathMLElement(tagName, document) {}

LayoutObject* MathMLRowElement::CreateLayoutObject(const ComputedStyle& style,
                                                   LegacyLayout legacy) {
  if (!RuntimeEnabledFeatures::MathMLCoreEnabled() ||
      !style.IsDisplayMathType() || legacy == LegacyLayout::kForce)
    return MathMLElement::CreateLayoutObject(style, legacy);
  return MakeGarbageCollected<LayoutNGMathMLBlock>(this);
}

void MathMLRowElement::ChildrenChanged(const ChildrenChange& change) {
  if (change.by_parser == ChildrenChangeSource::kAPI) {
    for (auto& child : Traversal<MathMLOperatorElement>::ChildrenOf(*this)) {
      // TODO(crbug.com/1124298): make this work for embellished operators.
      child.CheckFormAfterSiblingChange();
    }
  }

  MathMLElement::ChildrenChanged(change);
}

}  // namespace blink
