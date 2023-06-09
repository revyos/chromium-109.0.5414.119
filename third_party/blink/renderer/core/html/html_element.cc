/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2004-2008, 2013, 2014 Apple Inc. All rights reserved.
 * Copyright (C) 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (C) 2011 Motorola Mobility. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "third_party/blink/renderer/core/html/html_element.h"

#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/bindings/core/v8/js_event_handler_for_content_attribute.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_union_stringtreatnullasemptystring_trustedscript.h"
#include "third_party/blink/renderer/core/css/css_color.h"
#include "third_party/blink/renderer/core/css/css_identifier_value.h"
#include "third_party/blink/renderer/core/css/css_numeric_literal_value.h"
#include "third_party/blink/renderer/core/css/css_property_names.h"
#include "third_party/blink/renderer/core/css/css_property_value_set.h"
#include "third_party/blink/renderer/core/css/css_ratio_value.h"
#include "third_party/blink/renderer/core/css/css_value_list.h"
#include "third_party/blink/renderer/core/css/style_change_reason.h"
#include "third_party/blink/renderer/core/css_value_keywords.h"
#include "third_party/blink/renderer/core/dom/document_fragment.h"
#include "third_party/blink/renderer/core/dom/element_rare_data.h"
#include "third_party/blink/renderer/core/dom/element_traversal.h"
#include "third_party/blink/renderer/core/dom/events/event_listener.h"
#include "third_party/blink/renderer/core/dom/events/simulated_click_options.h"
#include "third_party/blink/renderer/core/dom/flat_tree_traversal.h"
#include "third_party/blink/renderer/core/dom/focus_params.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/dom/node_lists_node_data.h"
#include "third_party/blink/renderer/core/dom/node_traversal.h"
#include "third_party/blink/renderer/core/dom/popover_data.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/dom/slot_assignment.h"
#include "third_party/blink/renderer/core/dom/slot_assignment_engine.h"
#include "third_party/blink/renderer/core/dom/slot_assignment_recalc_forbidden_scope.h"
#include "third_party/blink/renderer/core/dom/text.h"
#include "third_party/blink/renderer/core/editing/editing_utilities.h"
#include "third_party/blink/renderer/core/editing/serializers/serialization.h"
#include "third_party/blink/renderer/core/editing/spellcheck/spell_checker.h"
#include "third_party/blink/renderer/core/event_type_names.h"
#include "third_party/blink/renderer/core/events/keyboard_event.h"
#include "third_party/blink/renderer/core/events/pointer_event.h"
#include "third_party/blink/renderer/core/frame/csp/content_security_policy.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/html/custom/custom_element.h"
#include "third_party/blink/renderer/core/html/custom/custom_element_registry.h"
#include "third_party/blink/renderer/core/html/custom/element_internals.h"
#include "third_party/blink/renderer/core/html/forms/html_form_element.h"
#include "third_party/blink/renderer/core/html/forms/html_input_element.h"
#include "third_party/blink/renderer/core/html/forms/labels_node_list.h"
#include "third_party/blink/renderer/core/html/html_br_element.h"
#include "third_party/blink/renderer/core/html/html_dimension.h"
#include "third_party/blink/renderer/core/html/html_document.h"
#include "third_party/blink/renderer/core/html/html_frame_owner_element.h"
#include "third_party/blink/renderer/core/html/html_slot_element.h"
#include "third_party/blink/renderer/core/html/html_template_element.h"
#include "third_party/blink/renderer/core/html/parser/html_parser_idioms.h"
#include "third_party/blink/renderer/core/html/shadow/shadow_element_names.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/input_type_names.h"
#include "third_party/blink/renderer/core/inspector/console_message.h"
#include "third_party/blink/renderer/core/layout/adjust_for_absolute_zoom.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/layout_box_model_object.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/mathml/mathml_element.h"
#include "third_party/blink/renderer/core/mathml_names.h"
#include "third_party/blink/renderer/core/page/spatial_navigation.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/svg/svg_svg_element.h"
#include "third_party/blink/renderer/core/timing/soft_navigation_heuristics.h"
#include "third_party/blink/renderer/core/trustedtypes/trusted_script.h"
#include "third_party/blink/renderer/core/xml_names.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"
#include "third_party/blink/renderer/platform/text/bidi_resolver.h"
#include "third_party/blink/renderer/platform/text/bidi_text_run.h"
#include "third_party/blink/renderer/platform/text/text_run_iterator.h"
#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"

namespace blink {

using AttributeChangedFunction =
    void (HTMLElement::*)(const Element::AttributeModificationParams& params);

struct AttributeTriggers {
  const QualifiedName& attribute;
  WebFeature web_feature;
  const AtomicString& event;
  AttributeChangedFunction function;
};

namespace {

// https://html.spec.whatwg.org/multipage/interaction.html#editing-host
// An editing host is either an HTML element with its contenteditable attribute
// in the true state, or a child HTML element of a Document whose design mode
// enabled is true.
// https://w3c.github.io/editing/execCommand.html#editable
// Something is editable if it is a node; it is not an editing host; it does not
// have a contenteditable attribute set to the false state; its parent is an
// editing host or editable; and either it is an HTML element, or it is an svg
// or math element, or it is not an Element and its parent is an HTML element.
bool IsEditableOrEditingHost(const Node& node) {
  auto* html_element = DynamicTo<HTMLElement>(node);
  if (html_element) {
    ContentEditableType content_editable =
        html_element->contentEditableNormalized();
    if (content_editable == ContentEditableType::kContentEditable ||
        content_editable == ContentEditableType::kPlaintextOnly)
      return true;
    if (html_element->GetDocument().InDesignMode() &&
        html_element->isConnected()) {
      return true;
    }
    if (content_editable == ContentEditableType::kNotContentEditable)
      return false;
  }
  if (!node.parentNode())
    return false;
  if (!IsEditableOrEditingHost(*node.parentNode()))
    return false;
  if (html_element)
    return true;
  if (IsA<SVGSVGElement>(node))
    return true;
  if (auto* mathml_element = DynamicTo<MathMLElement>(node))
    return mathml_element->HasTagName(mathml_names::kMathTag);
  return !IsA<Element>(node) && node.parentNode()->IsHTMLElement();
}

const WebFeature kNoWebFeature = static_cast<WebFeature>(0);

HTMLElement* GetParentForDirectionality(const HTMLElement& element,
                                        bool& needs_slot_assignment_recalc) {
  if (element.IsPseudoElement())
    return DynamicTo<HTMLElement>(element.ParentOrShadowHostNode());

  if (element.IsChildOfShadowHost()) {
    ShadowRoot* root = element.ShadowRootOfParent();
    if (!root || !root->HasSlotAssignment())
      return nullptr;

    if (root->NeedsSlotAssignmentRecalc()) {
      needs_slot_assignment_recalc = true;
      return nullptr;
    }
  }
  if (auto* parent_slot = ToHTMLSlotElementIfSupportsAssignmentOrNull(
          element.parentElement())) {
    ShadowRoot* root = parent_slot->ContainingShadowRoot();
    if (root->NeedsSlotAssignmentRecalc()) {
      needs_slot_assignment_recalc = true;
      return nullptr;
    }
  }

  // We should take care of all cases that would trigger a slot assignment
  // recalc, and delay the check for later for a performance reason.
  SlotAssignmentRecalcForbiddenScope forbid_slot_recalc(element.GetDocument());
  return DynamicTo<HTMLElement>(FlatTreeTraversal::ParentElement(element));
}

void CheckSoftNavigationHeuristicsTracking(const Document& document,
                                           const Node* insertion_point) {
  DCHECK(insertion_point);
  if (document.IsTrackingSoftNavigationHeuristics()) {
    LocalDOMWindow* window = document.domWindow();
    if (!window) {
      return;
    }
    LocalFrame* frame = window->GetFrame();
    if (!frame || !frame->IsMainFrame()) {
      return;
    }
    ScriptState* script_state = ToScriptStateForMainWorld(frame);
    if (!script_state) {
      return;
    }

    SoftNavigationHeuristics* heuristics =
        SoftNavigationHeuristics::From(*window);
    DCHECK(heuristics);
    heuristics->ModifiedDOM(script_state);
  }
}

}  // anonymous namespace

String HTMLElement::DebugNodeName() const {
  if (IsA<HTMLDocument>(GetDocument())) {
    return TagQName().HasPrefix() ? Element::nodeName().UpperASCII()
                                  : TagQName().LocalName().UpperASCII();
  }
  return Element::nodeName();
}

String HTMLElement::nodeName() const {
  // localNameUpper may intern and cache an AtomicString.
  DCHECK(IsMainThread());

  // FIXME: Would be nice to have an atomicstring lookup based off uppercase
  // chars that does not have to copy the string on a hit in the hash.
  // FIXME: We should have a way to detect XHTML elements and replace the
  // hasPrefix() check with it.
  if (IsA<HTMLDocument>(GetDocument())) {
    if (!TagQName().HasPrefix())
      return TagQName().LocalNameUpper();
    return Element::nodeName().UpperASCII();
  }
  return Element::nodeName();
}

bool HTMLElement::ShouldSerializeEndTag() const {
  // See https://www.w3.org/TR/DOM-Parsing/
  if (HasTagName(html_names::kAreaTag) || HasTagName(html_names::kBaseTag) ||
      HasTagName(html_names::kBasefontTag) ||
      HasTagName(html_names::kBgsoundTag) || HasTagName(html_names::kBrTag) ||
      HasTagName(html_names::kColTag) || HasTagName(html_names::kEmbedTag) ||
      HasTagName(html_names::kFrameTag) || HasTagName(html_names::kHrTag) ||
      HasTagName(html_names::kImgTag) || HasTagName(html_names::kInputTag) ||
      HasTagName(html_names::kKeygenTag) || HasTagName(html_names::kLinkTag) ||
      HasTagName(html_names::kMetaTag) || HasTagName(html_names::kParamTag) ||
      HasTagName(html_names::kSourceTag) || HasTagName(html_names::kTrackTag) ||
      HasTagName(html_names::kWbrTag))
    return false;
  return true;
}

static inline CSSValueID UnicodeBidiAttributeForDirAuto(HTMLElement* element) {
  if (element->HasTagName(html_names::kPreTag) ||
      element->HasTagName(html_names::kTextareaTag))
    return CSSValueID::kPlaintext;
  // FIXME: For bdo element, dir="auto" should result in "bidi-override isolate"
  // but we don't support having multiple values in unicode-bidi yet.
  // See https://bugs.webkit.org/show_bug.cgi?id=73164.
  return CSSValueID::kIsolate;
}

unsigned HTMLElement::ParseBorderWidthAttribute(
    const AtomicString& value) const {
  unsigned border_width = 0;
  if (value.empty() || !ParseHTMLNonNegativeInteger(value, border_width)) {
    if (HasTagName(html_names::kTableTag) && !value.IsNull())
      return 1;
  }
  return border_width;
}

void HTMLElement::ApplyBorderAttributeToStyle(
    const AtomicString& value,
    MutableCSSPropertyValueSet* style) {
  AddPropertyToPresentationAttributeStyle(style, CSSPropertyID::kBorderWidth,
                                          ParseBorderWidthAttribute(value),
                                          CSSPrimitiveValue::UnitType::kPixels);
  AddPropertyToPresentationAttributeStyle(style, CSSPropertyID::kBorderStyle,
                                          CSSValueID::kSolid);
}

bool HTMLElement::IsPresentationAttribute(const QualifiedName& name) const {
  if (name == html_names::kAlignAttr ||
      name == html_names::kContenteditableAttr ||
      name == html_names::kHiddenAttr || name == html_names::kLangAttr ||
      name.Matches(xml_names::kLangAttr) ||
      name == html_names::kDraggableAttr || name == html_names::kDirAttr ||
      name == html_names::kInertAttr)
    return true;
  return Element::IsPresentationAttribute(name);
}

static inline bool IsValidDirAttribute(const AtomicString& value) {
  return EqualIgnoringASCIICase(value, "auto") ||
         EqualIgnoringASCIICase(value, "ltr") ||
         EqualIgnoringASCIICase(value, "rtl");
}

void HTMLElement::CollectStyleForPresentationAttribute(
    const QualifiedName& name,
    const AtomicString& value,
    MutableCSSPropertyValueSet* style) {
  if (name == html_names::kAlignAttr) {
    if (EqualIgnoringASCIICase(value, "middle")) {
      AddPropertyToPresentationAttributeStyle(style, CSSPropertyID::kTextAlign,
                                              CSSValueID::kCenter);
    } else {
      AddPropertyToPresentationAttributeStyle(style, CSSPropertyID::kTextAlign,
                                              value);
    }
  } else if (name == html_names::kContenteditableAttr) {
    if (value.empty() || EqualIgnoringASCIICase(value, "true")) {
      AddPropertyToPresentationAttributeStyle(
          style, CSSPropertyID::kWebkitUserModify, CSSValueID::kReadWrite);
      AddPropertyToPresentationAttributeStyle(
          style, CSSPropertyID::kOverflowWrap, CSSValueID::kBreakWord);
      AddPropertyToPresentationAttributeStyle(
          style, CSSPropertyID::kWebkitLineBreak, CSSValueID::kAfterWhiteSpace);
      UseCounter::Count(GetDocument(), WebFeature::kContentEditableTrue);
      if (HasTagName(html_names::kHTMLTag)) {
        UseCounter::Count(GetDocument(),
                          WebFeature::kContentEditableTrueOnHTML);
      }
    } else if (EqualIgnoringASCIICase(value, "plaintext-only")) {
      AddPropertyToPresentationAttributeStyle(
          style, CSSPropertyID::kWebkitUserModify,
          CSSValueID::kReadWritePlaintextOnly);
      AddPropertyToPresentationAttributeStyle(
          style, CSSPropertyID::kOverflowWrap, CSSValueID::kBreakWord);
      AddPropertyToPresentationAttributeStyle(
          style, CSSPropertyID::kWebkitLineBreak, CSSValueID::kAfterWhiteSpace);
      UseCounter::Count(GetDocument(),
                        WebFeature::kContentEditablePlainTextOnly);
    } else if (EqualIgnoringASCIICase(value, "false")) {
      AddPropertyToPresentationAttributeStyle(
          style, CSSPropertyID::kWebkitUserModify, CSSValueID::kReadOnly);
    }
  } else if (name == html_names::kHiddenAttr) {
    if (RuntimeEnabledFeatures::BeforeMatchEventEnabled(
            GetExecutionContext()) &&
        EqualIgnoringASCIICase(value, "until-found")) {
      AddPropertyToPresentationAttributeStyle(
          style, CSSPropertyID::kContentVisibility, CSSValueID::kHidden);
      UseCounter::Count(GetDocument(), WebFeature::kHiddenUntilFoundAttribute);
    } else {
      AddPropertyToPresentationAttributeStyle(style, CSSPropertyID::kDisplay,
                                              CSSValueID::kNone);
      UseCounter::Count(GetDocument(), WebFeature::kHiddenAttribute);
    }
  } else if (name == html_names::kDraggableAttr) {
    UseCounter::Count(GetDocument(), WebFeature::kDraggableAttribute);
    if (EqualIgnoringASCIICase(value, "true")) {
      AddPropertyToPresentationAttributeStyle(
          style, CSSPropertyID::kWebkitUserDrag, CSSValueID::kElement);
      AddPropertyToPresentationAttributeStyle(style, CSSPropertyID::kUserSelect,
                                              CSSValueID::kNone);
    } else if (EqualIgnoringASCIICase(value, "false")) {
      AddPropertyToPresentationAttributeStyle(
          style, CSSPropertyID::kWebkitUserDrag, CSSValueID::kNone);
    }
  } else if (name == html_names::kDirAttr) {
    if (EqualIgnoringASCIICase(value, "auto")) {
      AddPropertyToPresentationAttributeStyle(
          style, CSSPropertyID::kUnicodeBidi,
          UnicodeBidiAttributeForDirAuto(this));
    } else {
      if (IsValidDirAttribute(value)) {
        AddPropertyToPresentationAttributeStyle(
            style, CSSPropertyID::kDirection, value);
      } else if (IsA<HTMLBodyElement>(*this)) {
        AddPropertyToPresentationAttributeStyle(
            style, CSSPropertyID::kDirection, "ltr");
      }
      if (!HasTagName(html_names::kBdiTag) &&
          !HasTagName(html_names::kBdoTag) &&
          !HasTagName(html_names::kOutputTag)) {
        AddPropertyToPresentationAttributeStyle(
            style, CSSPropertyID::kUnicodeBidi, CSSValueID::kIsolate);
      }
    }
  } else if (name.Matches(xml_names::kLangAttr)) {
    MapLanguageAttributeToLocale(value, style);
  } else if (name == html_names::kLangAttr) {
    // xml:lang has a higher priority than lang.
    if (!FastHasAttribute(xml_names::kLangAttr))
      MapLanguageAttributeToLocale(value, style);
  } else {
    Element::CollectStyleForPresentationAttribute(name, value, style);
  }
}

// static
AttributeTriggers* HTMLElement::TriggersForAttributeName(
    const QualifiedName& attr_name) {
  const AtomicString& kNoEvent = g_null_atom;
  static AttributeTriggers attribute_triggers[] = {
      {html_names::kDirAttr, kNoWebFeature, kNoEvent,
       &HTMLElement::OnDirAttrChanged},
      {html_names::kFormAttr, kNoWebFeature, kNoEvent,
       &HTMLElement::OnFormAttrChanged},
      {html_names::kLangAttr, kNoWebFeature, kNoEvent,
       &HTMLElement::OnLangAttrChanged},
      {html_names::kNonceAttr, kNoWebFeature, kNoEvent,
       &HTMLElement::OnNonceAttrChanged},

      {html_names::kFocusgroupAttr, kNoWebFeature, kNoEvent,
       &HTMLElement::ReparseAttribute},
      {html_names::kTabindexAttr, kNoWebFeature, kNoEvent,
       &HTMLElement::ReparseAttribute},
      {xml_names::kLangAttr, kNoWebFeature, kNoEvent,
       &HTMLElement::ReparseAttribute},
      {html_names::kPopoverAttr, kNoWebFeature, kNoEvent,
       &HTMLElement::ReparseAttribute},

      {html_names::kOnabortAttr, kNoWebFeature, event_type_names::kAbort,
       nullptr},
      {html_names::kOnanimationendAttr, kNoWebFeature,
       event_type_names::kAnimationend, nullptr},
      {html_names::kOnanimationiterationAttr, kNoWebFeature,
       event_type_names::kAnimationiteration, nullptr},
      {html_names::kOnanimationstartAttr, kNoWebFeature,
       event_type_names::kAnimationstart, nullptr},
      {html_names::kOnauxclickAttr, kNoWebFeature, event_type_names::kAuxclick,
       nullptr},
      {html_names::kOnbeforecopyAttr, kNoWebFeature,
       event_type_names::kBeforecopy, nullptr},
      {html_names::kOnbeforecutAttr, kNoWebFeature,
       event_type_names::kBeforecut, nullptr},
      {html_names::kOnbeforeinputAttr, kNoWebFeature,
       event_type_names::kBeforeinput, nullptr},
      {html_names::kOnbeforepasteAttr, kNoWebFeature,
       event_type_names::kBeforepaste, nullptr},
      {html_names::kOnblurAttr, kNoWebFeature, event_type_names::kBlur,
       nullptr},
      {html_names::kOncancelAttr, kNoWebFeature, event_type_names::kCancel,
       nullptr},
      {html_names::kOncanplayAttr, kNoWebFeature, event_type_names::kCanplay,
       nullptr},
      {html_names::kOncanplaythroughAttr, kNoWebFeature,
       event_type_names::kCanplaythrough, nullptr},
      {html_names::kOnchangeAttr, kNoWebFeature, event_type_names::kChange,
       nullptr},
      {html_names::kOnclickAttr, kNoWebFeature, event_type_names::kClick,
       nullptr},
      {html_names::kOncloseAttr, kNoWebFeature, event_type_names::kClose,
       nullptr},
      {html_names::kOncontextlostAttr, kNoWebFeature,
       event_type_names::kContextlost, nullptr},
      {html_names::kOncontextmenuAttr, kNoWebFeature,
       event_type_names::kContextmenu, nullptr},
      {html_names::kOncontextrestoredAttr, kNoWebFeature,
       event_type_names::kContextrestored, nullptr},
      {html_names::kOncopyAttr, kNoWebFeature, event_type_names::kCopy,
       nullptr},
      {html_names::kOncuechangeAttr, kNoWebFeature,
       event_type_names::kCuechange, nullptr},
      {html_names::kOncutAttr, kNoWebFeature, event_type_names::kCut, nullptr},
      {html_names::kOndblclickAttr, kNoWebFeature, event_type_names::kDblclick,
       nullptr},
      {html_names::kOndragAttr, kNoWebFeature, event_type_names::kDrag,
       nullptr},
      {html_names::kOndragendAttr, kNoWebFeature, event_type_names::kDragend,
       nullptr},
      {html_names::kOndragenterAttr, kNoWebFeature,
       event_type_names::kDragenter, nullptr},
      {html_names::kOndragleaveAttr, kNoWebFeature,
       event_type_names::kDragleave, nullptr},
      {html_names::kOndragoverAttr, kNoWebFeature, event_type_names::kDragover,
       nullptr},
      {html_names::kOndragstartAttr, kNoWebFeature,
       event_type_names::kDragstart, nullptr},
      {html_names::kOndropAttr, kNoWebFeature, event_type_names::kDrop,
       nullptr},
      {html_names::kOndurationchangeAttr, kNoWebFeature,
       event_type_names::kDurationchange, nullptr},
      {html_names::kOnemptiedAttr, kNoWebFeature, event_type_names::kEmptied,
       nullptr},
      {html_names::kOnendedAttr, kNoWebFeature, event_type_names::kEnded,
       nullptr},
      {html_names::kOnerrorAttr, kNoWebFeature, event_type_names::kError,
       nullptr},
      {html_names::kOnfocusAttr, kNoWebFeature, event_type_names::kFocus,
       nullptr},
      {html_names::kOnfocusinAttr, kNoWebFeature, event_type_names::kFocusin,
       nullptr},
      {html_names::kOnfocusoutAttr, kNoWebFeature, event_type_names::kFocusout,
       nullptr},
      {html_names::kOnformdataAttr, kNoWebFeature, event_type_names::kFormdata,
       nullptr},
      {html_names::kOngotpointercaptureAttr, kNoWebFeature,
       event_type_names::kGotpointercapture, nullptr},
      {html_names::kOninputAttr, kNoWebFeature, event_type_names::kInput,
       nullptr},
      {html_names::kOninvalidAttr, kNoWebFeature, event_type_names::kInvalid,
       nullptr},
      {html_names::kOnkeydownAttr, kNoWebFeature, event_type_names::kKeydown,
       nullptr},
      {html_names::kOnkeypressAttr, kNoWebFeature, event_type_names::kKeypress,
       nullptr},
      {html_names::kOnkeyupAttr, kNoWebFeature, event_type_names::kKeyup,
       nullptr},
      {html_names::kOnloadAttr, kNoWebFeature, event_type_names::kLoad,
       nullptr},
      {html_names::kOnloadeddataAttr, kNoWebFeature,
       event_type_names::kLoadeddata, nullptr},
      {html_names::kOnloadedmetadataAttr, kNoWebFeature,
       event_type_names::kLoadedmetadata, nullptr},
      {html_names::kOnloadstartAttr, kNoWebFeature,
       event_type_names::kLoadstart, nullptr},
      {html_names::kOnlostpointercaptureAttr, kNoWebFeature,
       event_type_names::kLostpointercapture, nullptr},
      {html_names::kOnmousedownAttr, kNoWebFeature,
       event_type_names::kMousedown, nullptr},
      {html_names::kOnmouseenterAttr, kNoWebFeature,
       event_type_names::kMouseenter, nullptr},
      {html_names::kOnmouseleaveAttr, kNoWebFeature,
       event_type_names::kMouseleave, nullptr},
      {html_names::kOnmousemoveAttr, kNoWebFeature,
       event_type_names::kMousemove, nullptr},
      {html_names::kOnmouseoutAttr, kNoWebFeature, event_type_names::kMouseout,
       nullptr},
      {html_names::kOnmouseoverAttr, kNoWebFeature,
       event_type_names::kMouseover, nullptr},
      {html_names::kOnmouseupAttr, kNoWebFeature, event_type_names::kMouseup,
       nullptr},
      {html_names::kOnmousewheelAttr, kNoWebFeature,
       event_type_names::kMousewheel, nullptr},
      {html_names::kOnoverscrollAttr, kNoWebFeature,
       event_type_names::kOverscroll, nullptr},
      {html_names::kOnpasteAttr, kNoWebFeature, event_type_names::kPaste,
       nullptr},
      {html_names::kOnpauseAttr, kNoWebFeature, event_type_names::kPause,
       nullptr},
      {html_names::kOnplayAttr, kNoWebFeature, event_type_names::kPlay,
       nullptr},
      {html_names::kOnplayingAttr, kNoWebFeature, event_type_names::kPlaying,
       nullptr},
      {html_names::kOnpointercancelAttr, kNoWebFeature,
       event_type_names::kPointercancel, nullptr},
      {html_names::kOnpointerdownAttr, kNoWebFeature,
       event_type_names::kPointerdown, nullptr},
      {html_names::kOnpointerenterAttr, kNoWebFeature,
       event_type_names::kPointerenter, nullptr},
      {html_names::kOnpointerleaveAttr, kNoWebFeature,
       event_type_names::kPointerleave, nullptr},
      {html_names::kOnpointermoveAttr, kNoWebFeature,
       event_type_names::kPointermove, nullptr},
      {html_names::kOnpointeroutAttr, kNoWebFeature,
       event_type_names::kPointerout, nullptr},
      {html_names::kOnpointeroverAttr, kNoWebFeature,
       event_type_names::kPointerover, nullptr},
      {html_names::kOnpointerrawupdateAttr, kNoWebFeature,
       event_type_names::kPointerrawupdate, nullptr},
      {html_names::kOnpointerupAttr, kNoWebFeature,
       event_type_names::kPointerup, nullptr},
      {html_names::kOnprogressAttr, kNoWebFeature, event_type_names::kProgress,
       nullptr},
      {html_names::kOnpopoverhideAttr, kNoWebFeature,
       event_type_names::kPopoverhide, nullptr},
      {html_names::kOnpopovershowAttr, kNoWebFeature,
       event_type_names::kPopovershow, nullptr},
      {html_names::kOnratechangeAttr, kNoWebFeature,
       event_type_names::kRatechange, nullptr},
      {html_names::kOnresetAttr, kNoWebFeature, event_type_names::kReset,
       nullptr},
      {html_names::kOnresizeAttr, kNoWebFeature, event_type_names::kResize,
       nullptr},
      {html_names::kOnscrollAttr, kNoWebFeature, event_type_names::kScroll,
       nullptr},
      {html_names::kOnscrollendAttr, kNoWebFeature,
       event_type_names::kScrollend, nullptr},
      {html_names::kOnseekedAttr, kNoWebFeature, event_type_names::kSeeked,
       nullptr},
      {html_names::kOnseekingAttr, kNoWebFeature, event_type_names::kSeeking,
       nullptr},
      {html_names::kOnsecuritypolicyviolationAttr, kNoWebFeature,
       event_type_names::kSecuritypolicyviolation, nullptr},
      {html_names::kOnselectAttr, kNoWebFeature, event_type_names::kSelect,
       nullptr},
      {html_names::kOnselectstartAttr, kNoWebFeature,
       event_type_names::kSelectstart, nullptr},
      {html_names::kOnslotchangeAttr, kNoWebFeature,
       event_type_names::kSlotchange, nullptr},
      {html_names::kOnstalledAttr, kNoWebFeature, event_type_names::kStalled,
       nullptr},
      {html_names::kOnsubmitAttr, kNoWebFeature, event_type_names::kSubmit,
       nullptr},
      {html_names::kOnsuspendAttr, kNoWebFeature, event_type_names::kSuspend,
       nullptr},
      {html_names::kOntimeupdateAttr, kNoWebFeature,
       event_type_names::kTimeupdate, nullptr},
      {html_names::kOntoggleAttr, kNoWebFeature, event_type_names::kToggle,
       nullptr},
      {html_names::kOntouchcancelAttr, kNoWebFeature,
       event_type_names::kTouchcancel, nullptr},
      {html_names::kOntouchendAttr, kNoWebFeature, event_type_names::kTouchend,
       nullptr},
      {html_names::kOntouchmoveAttr, kNoWebFeature,
       event_type_names::kTouchmove, nullptr},
      {html_names::kOntouchstartAttr, kNoWebFeature,
       event_type_names::kTouchstart, nullptr},
      {html_names::kOntransitionendAttr, kNoWebFeature,
       event_type_names::kWebkitTransitionEnd, nullptr},
      {html_names::kOnvolumechangeAttr, kNoWebFeature,
       event_type_names::kVolumechange, nullptr},
      {html_names::kOnwaitingAttr, kNoWebFeature, event_type_names::kWaiting,
       nullptr},
      {html_names::kOnwebkitanimationendAttr, kNoWebFeature,
       event_type_names::kWebkitAnimationEnd, nullptr},
      {html_names::kOnwebkitanimationiterationAttr, kNoWebFeature,
       event_type_names::kWebkitAnimationIteration, nullptr},
      {html_names::kOnwebkitanimationstartAttr, kNoWebFeature,
       event_type_names::kWebkitAnimationStart, nullptr},
      {html_names::kOnwebkitfullscreenchangeAttr, kNoWebFeature,
       event_type_names::kWebkitfullscreenchange, nullptr},
      {html_names::kOnwebkitfullscreenerrorAttr, kNoWebFeature,
       event_type_names::kWebkitfullscreenerror, nullptr},
      {html_names::kOnwebkittransitionendAttr, kNoWebFeature,
       event_type_names::kWebkitTransitionEnd, nullptr},
      {html_names::kOnwheelAttr, kNoWebFeature, event_type_names::kWheel,
       nullptr},

      // Begin ARIA attributes.
      {html_names::kAriaActivedescendantAttr,
       WebFeature::kARIAActiveDescendantAttribute, kNoEvent, nullptr},
      {html_names::kAriaAtomicAttr, WebFeature::kARIAAtomicAttribute, kNoEvent,
       nullptr},
      {html_names::kAriaAutocompleteAttr,
       WebFeature::kARIAAutocompleteAttribute, kNoEvent, nullptr},
      {html_names::kAriaBusyAttr, WebFeature::kARIABusyAttribute, kNoEvent,
       nullptr},
      {html_names::kAriaCheckedAttr, WebFeature::kARIACheckedAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaColcountAttr, WebFeature::kARIAColCountAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaColindexAttr, WebFeature::kARIAColIndexAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaColspanAttr, WebFeature::kARIAColSpanAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaControlsAttr, WebFeature::kARIAControlsAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaCurrentAttr, WebFeature::kARIACurrentAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaDescribedbyAttr, WebFeature::kARIADescribedByAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaDescriptionAttr, WebFeature::kARIADescriptionAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaDetailsAttr, WebFeature::kARIADetailsAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaDisabledAttr, WebFeature::kARIADisabledAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaDropeffectAttr, WebFeature::kARIADropEffectAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaErrormessageAttr,
       WebFeature::kARIAErrorMessageAttribute, kNoEvent, nullptr},
      {html_names::kAriaExpandedAttr, WebFeature::kARIAExpandedAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaFlowtoAttr, WebFeature::kARIAFlowToAttribute, kNoEvent,
       nullptr},
      {html_names::kAriaGrabbedAttr, WebFeature::kARIAGrabbedAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaHaspopupAttr, WebFeature::kARIAHasPopupAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaHiddenAttr, WebFeature::kARIAHiddenAttribute, kNoEvent,
       nullptr},
      {html_names::kAriaInvalidAttr, WebFeature::kARIAInvalidAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaKeyshortcutsAttr,
       WebFeature::kARIAKeyShortcutsAttribute, kNoEvent, nullptr},
      {html_names::kAriaLabelAttr, WebFeature::kARIALabelAttribute, kNoEvent,
       nullptr},
      {html_names::kAriaLabeledbyAttr, WebFeature::kARIALabeledByAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaLabelledbyAttr, WebFeature::kARIALabelledByAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaLevelAttr, WebFeature::kARIALevelAttribute, kNoEvent,
       nullptr},
      {html_names::kAriaLiveAttr, WebFeature::kARIALiveAttribute, kNoEvent,
       nullptr},
      {html_names::kAriaModalAttr, WebFeature::kARIAModalAttribute, kNoEvent,
       nullptr},
      {html_names::kAriaMultilineAttr, WebFeature::kARIAMultilineAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaMultiselectableAttr,
       WebFeature::kARIAMultiselectableAttribute, kNoEvent, nullptr},
      {html_names::kAriaOrientationAttr, WebFeature::kARIAOrientationAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaOwnsAttr, WebFeature::kARIAOwnsAttribute, kNoEvent,
       nullptr},
      {html_names::kAriaPlaceholderAttr, WebFeature::kARIAPlaceholderAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaPosinsetAttr, WebFeature::kARIAPosInSetAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaPressedAttr, WebFeature::kARIAPressedAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaReadonlyAttr, WebFeature::kARIAReadOnlyAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaRelevantAttr, WebFeature::kARIARelevantAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaRequiredAttr, WebFeature::kARIARequiredAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaRoledescriptionAttr,
       WebFeature::kARIARoleDescriptionAttribute, kNoEvent, nullptr},
      {html_names::kAriaRowcountAttr, WebFeature::kARIARowCountAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaRowindexAttr, WebFeature::kARIARowIndexAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaRowspanAttr, WebFeature::kARIARowSpanAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaSelectedAttr, WebFeature::kARIASelectedAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaSetsizeAttr, WebFeature::kARIASetSizeAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaSortAttr, WebFeature::kARIASortAttribute, kNoEvent,
       nullptr},
      {html_names::kAriaTouchpassthroughAttr,
       WebFeature::kARIATouchpassthroughAttribute, kNoEvent, nullptr},
      {html_names::kAriaValuemaxAttr, WebFeature::kARIAValueMaxAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaValueminAttr, WebFeature::kARIAValueMinAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaValuenowAttr, WebFeature::kARIAValueNowAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaValuetextAttr, WebFeature::kARIAValueTextAttribute,
       kNoEvent, nullptr},
      {html_names::kAriaVirtualcontentAttr,
       WebFeature::kARIAVirtualcontentAttribute, kNoEvent, nullptr},
      // End ARIA attributes.

      {html_names::kAutocapitalizeAttr, WebFeature::kAutocapitalizeAttribute,
       kNoEvent, nullptr},
  };

  using AttributeToTriggerIndexMap = HashMap<QualifiedName, uint32_t>;
  DEFINE_STATIC_LOCAL(AttributeToTriggerIndexMap,
                      attribute_to_trigger_index_map, ());
  if (!attribute_to_trigger_index_map.size()) {
    for (uint32_t i = 0; i < std::size(attribute_triggers); ++i)
      attribute_to_trigger_index_map.insert(attribute_triggers[i].attribute, i);
  }

  auto iter = attribute_to_trigger_index_map.find(attr_name);
  if (iter != attribute_to_trigger_index_map.end())
    return &attribute_triggers[iter->value];
  return nullptr;
}

// static
const AtomicString& HTMLElement::EventNameForAttributeName(
    const QualifiedName& attr_name) {
  AttributeTriggers* triggers = TriggersForAttributeName(attr_name);
  if (triggers)
    return triggers->event;
  return g_null_atom;
}

void HTMLElement::AttributeChanged(const AttributeModificationParams& params) {
  Element::AttributeChanged(params);
  if (params.name == html_names::kDisabledAttr &&
      IsFormAssociatedCustomElement() &&
      params.old_value.IsNull() != params.new_value.IsNull()) {
    EnsureElementInternals().DisabledAttributeChanged();
    if (params.reason == AttributeModificationReason::kDirectly &&
        IsDisabledFormControl() && AdjustedFocusedElementInTreeScope() == this)
      blur();
    return;
  }
  if (params.name == html_names::kReadonlyAttr &&
      IsFormAssociatedCustomElement() &&
      params.old_value.IsNull() != params.new_value.IsNull()) {
    EnsureElementInternals().ReadonlyAttributeChanged();
    return;
  }
  if (params.reason == AttributeModificationReason::kByParser &&
      params.name == html_names::kDefaultopenAttr && HasPopoverAttribute()) {
    DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
        GetDocument().GetExecutionContext()));
    DCHECK(!isConnected());
    GetPopoverData()->setHadDefaultOpenWhenParsed(true);
  }

  if (params.reason != AttributeModificationReason::kDirectly)
    return;
  // adjustedFocusedElementInTreeScope() is not trivial. We should check
  // attribute names, then call adjustedFocusedElementInTreeScope().
  if (params.name == html_names::kHiddenAttr && !params.new_value.IsNull()) {
    if (AdjustedFocusedElementInTreeScope() == this)
      blur();
  } else if (params.name == html_names::kSpellcheckAttr) {
    if (GetDocument().GetFrame()) {
      GetDocument().GetFrame()->GetSpellChecker().RespondToChangedEnablement(
          *this, IsSpellCheckingEnabled());
    }
  } else if (params.name == html_names::kContenteditableAttr) {
    if (GetDocument().GetFrame()) {
      GetDocument()
          .GetFrame()
          ->GetSpellChecker()
          .RemoveSpellingAndGrammarMarkers(
              *this, SpellChecker::ElementsType::kOnlyNonEditable);
    }
    if (AdjustedFocusedElementInTreeScope() != this)
      return;
    // The attribute change may cause supportsFocus() to return false
    // for the element which had focus.
    //
    // TODO(tkent): We should avoid updating style.  We'd like to check only
    // DOM-level focusability here.
    GetDocument().UpdateStyleAndLayoutTreeForNode(this);
    if (!SupportsFocus())
      blur();
  }
}

void HTMLElement::ParseAttribute(const AttributeModificationParams& params) {
  AttributeTriggers* triggers = TriggersForAttributeName(params.name);
  if (!triggers)
    return;

  if (triggers->event != g_null_atom) {
    SetAttributeEventListener(
        triggers->event,
        JSEventHandlerForContentAttribute::Create(
            GetExecutionContext(), params.name, params.new_value));
  }

  if (triggers->web_feature != kNoWebFeature) {
    // Count usage of attributes but ignore attributes in user agent shadow DOM.
    if (!IsInUserAgentShadowRoot())
      UseCounter::Count(GetDocument(), triggers->web_feature);
  }
  if (triggers->function)
    ((*this).*(triggers->function))(params);
}

DocumentFragment* HTMLElement::TextToFragment(const String& text,
                                              ExceptionState& exception_state) {
  DocumentFragment* fragment = DocumentFragment::Create(GetDocument());
  unsigned i, length = text.length();
  UChar c = 0;
  for (unsigned start = 0; start < length;) {
    // Find next line break.
    for (i = start; i < length; i++) {
      c = text[i];
      if (c == '\r' || c == '\n')
        break;
    }

    if (i > start) {
      fragment->AppendChild(
          Text::Create(GetDocument(), text.Substring(start, i - start)),
          exception_state);
      if (exception_state.HadException())
        return nullptr;
    }

    if (i == length)
      break;

    fragment->AppendChild(MakeGarbageCollected<HTMLBRElement>(GetDocument()),
                          exception_state);
    if (exception_state.HadException())
      return nullptr;

    // Make sure \r\n doesn't result in two line breaks.
    if (c == '\r' && i + 1 < length && text[i + 1] == '\n')
      i++;

    start = i + 1;  // Character after line break.
  }

  return fragment;
}

V8UnionStringTreatNullAsEmptyStringOrTrustedScript*
HTMLElement::innerTextForBinding() {
  return MakeGarbageCollected<
      V8UnionStringTreatNullAsEmptyStringOrTrustedScript>(innerText());
}

void HTMLElement::setInnerTextForBinding(
    const V8UnionStringTreatNullAsEmptyStringOrTrustedScript*
        string_or_trusted_script,
    ExceptionState& exception_state) {
  String value;
  switch (string_or_trusted_script->GetContentType()) {
    case V8UnionStringTreatNullAsEmptyStringOrTrustedScript::ContentType::
        kStringTreatNullAsEmptyString:
      value = string_or_trusted_script->GetAsStringTreatNullAsEmptyString();
      break;
    case V8UnionStringTreatNullAsEmptyStringOrTrustedScript::ContentType::
        kTrustedScript:
      value = string_or_trusted_script->GetAsTrustedScript()->toString();
      break;
  }
  setInnerText(value);
}

String HTMLElement::innerText() {
  return Element::innerText();
}

void HTMLElement::setInnerText(const String& text) {
  // FIXME: This doesn't take whitespace collapsing into account at all.

  if (!text.Contains('\n') && !text.Contains('\r')) {
    if (text.empty()) {
      RemoveChildren();
      return;
    }
    ReplaceChildrenWithText(this, text, ASSERT_NO_EXCEPTION);
    return;
  }

  // Add text nodes and <br> elements.
  DocumentFragment* fragment = TextToFragment(text, ASSERT_NO_EXCEPTION);
  ReplaceChildrenWithFragment(this, fragment, ASSERT_NO_EXCEPTION);
}

void HTMLElement::setOuterText(const String& text,
                               ExceptionState& exception_state) {
  ContainerNode* parent = parentNode();
  if (!parent) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNoModificationAllowedError,
        "The element has no parent.");
    return;
  }

  Node* prev = previousSibling();
  Node* next = nextSibling();
  Node* new_child = nullptr;

  // Convert text to fragment with <br> tags instead of linebreaks if needed.
  if (text.Contains('\r') || text.Contains('\n'))
    new_child = TextToFragment(text, exception_state);
  else
    new_child = Text::Create(GetDocument(), text);

  if (exception_state.HadException())
    return;

  parent->ReplaceChild(new_child, this, exception_state);

  Node* node = next ? next->previousSibling() : nullptr;
  auto* next_text_node = DynamicTo<Text>(node);
  if (!exception_state.HadException() && next_text_node)
    MergeWithNextTextNode(next_text_node, exception_state);

  auto* prev_text_node = DynamicTo<Text>(prev);
  if (!exception_state.HadException() && prev && prev->IsTextNode())
    MergeWithNextTextNode(prev_text_node, exception_state);
}

void HTMLElement::ApplyAspectRatioToStyle(const AtomicString& width,
                                          const AtomicString& height,
                                          MutableCSSPropertyValueSet* style) {
  HTMLDimension width_dim;
  if (!ParseDimensionValue(width, width_dim) || !width_dim.IsAbsolute())
    return;
  HTMLDimension height_dim;
  if (!ParseDimensionValue(height, height_dim) || !height_dim.IsAbsolute())
    return;
  ApplyAspectRatioToStyle(width_dim.Value(), height_dim.Value(), style);
}

void HTMLElement::ApplyIntegerAspectRatioToStyle(
    const AtomicString& width,
    const AtomicString& height,
    MutableCSSPropertyValueSet* style) {
  unsigned width_val = 0;
  if (!ParseHTMLNonNegativeInteger(width, width_val))
    return;
  unsigned height_val = 0;
  if (!ParseHTMLNonNegativeInteger(height, height_val))
    return;
  ApplyAspectRatioToStyle(width_val, height_val, style);
}

void HTMLElement::ApplyAspectRatioToStyle(double width,
                                          double height,
                                          MutableCSSPropertyValueSet* style) {
  auto* width_val = CSSNumericLiteralValue::Create(
      width, CSSPrimitiveValue::UnitType::kNumber);
  auto* height_val = CSSNumericLiteralValue::Create(
      height, CSSPrimitiveValue::UnitType::kNumber);
  auto* ratio_value =
      MakeGarbageCollected<cssvalue::CSSRatioValue>(*width_val, *height_val);

  CSSValueList* list = CSSValueList::CreateSpaceSeparated();
  list->Append(*CSSIdentifierValue::Create(CSSValueID::kAuto));
  list->Append(*ratio_value);

  style->SetProperty(CSSPropertyID::kAspectRatio, *list);
}

void HTMLElement::ApplyAlignmentAttributeToStyle(
    const AtomicString& alignment,
    MutableCSSPropertyValueSet* style) {
  // Vertical alignment with respect to the current baseline of the text
  // right or left means floating images.
  CSSValueID float_value = CSSValueID::kInvalid;
  CSSValueID vertical_align_value = CSSValueID::kInvalid;

  if (EqualIgnoringASCIICase(alignment, "absmiddle") ||
      EqualIgnoringASCIICase(alignment, "abscenter")) {
    vertical_align_value = CSSValueID::kMiddle;
  } else if (EqualIgnoringASCIICase(alignment, "absbottom")) {
    vertical_align_value = CSSValueID::kBottom;
  } else if (EqualIgnoringASCIICase(alignment, "left")) {
    float_value = CSSValueID::kLeft;
    vertical_align_value = CSSValueID::kTop;
  } else if (EqualIgnoringASCIICase(alignment, "right")) {
    float_value = CSSValueID::kRight;
    vertical_align_value = CSSValueID::kTop;
  } else if (EqualIgnoringASCIICase(alignment, "top")) {
    vertical_align_value = CSSValueID::kTop;
  } else if (EqualIgnoringASCIICase(alignment, "middle")) {
    vertical_align_value = CSSValueID::kWebkitBaselineMiddle;
  } else if (EqualIgnoringASCIICase(alignment, "center")) {
    vertical_align_value = CSSValueID::kMiddle;
  } else if (EqualIgnoringASCIICase(alignment, "bottom")) {
    vertical_align_value = CSSValueID::kBaseline;
  } else if (EqualIgnoringASCIICase(alignment, "texttop")) {
    vertical_align_value = CSSValueID::kTextTop;
  }

  if (IsValidCSSValueID(float_value)) {
    AddPropertyToPresentationAttributeStyle(style, CSSPropertyID::kFloat,
                                            float_value);
  }

  if (IsValidCSSValueID(vertical_align_value)) {
    AddPropertyToPresentationAttributeStyle(
        style, CSSPropertyID::kVerticalAlign, vertical_align_value);
  }
}

bool HTMLElement::HasCustomFocusLogic() const {
  return false;
}

ContentEditableType HTMLElement::contentEditableNormalized() const {
  const AtomicString& value =
      FastGetAttribute(html_names::kContenteditableAttr);

  if (value.IsNull())
    return ContentEditableType::kInherit;
  if (value.empty() || EqualIgnoringASCIICase(value, "true"))
    return ContentEditableType::kContentEditable;
  if (EqualIgnoringASCIICase(value, "false"))
    return ContentEditableType::kNotContentEditable;
  if (EqualIgnoringASCIICase(value, "plaintext-only"))
    return ContentEditableType::kPlaintextOnly;

  return ContentEditableType::kInherit;
}

String HTMLElement::contentEditable() const {
  switch (contentEditableNormalized()) {
    case ContentEditableType::kInherit:
      return "inherit";
    case ContentEditableType::kContentEditable:
      return "true";
    case ContentEditableType::kNotContentEditable:
      return "false";
    case ContentEditableType::kPlaintextOnly:
      return "plaintext-only";
  }
}

void HTMLElement::setContentEditable(const String& enabled,
                                     ExceptionState& exception_state) {
  if (EqualIgnoringASCIICase(enabled, "true"))
    setAttribute(html_names::kContenteditableAttr, "true");
  else if (EqualIgnoringASCIICase(enabled, "false"))
    setAttribute(html_names::kContenteditableAttr, "false");
  else if (EqualIgnoringASCIICase(enabled, "plaintext-only"))
    setAttribute(html_names::kContenteditableAttr, "plaintext-only");
  else if (EqualIgnoringASCIICase(enabled, "inherit"))
    removeAttribute(html_names::kContenteditableAttr);
  else
    exception_state.ThrowDOMException(DOMExceptionCode::kSyntaxError,
                                      "The value provided ('" + enabled +
                                          "') is not one of 'true', 'false', "
                                          "'plaintext-only', or 'inherit'.");
}

V8UnionBooleanOrStringOrUnrestrictedDouble* HTMLElement::hidden() const {
  const AtomicString& attribute = FastGetAttribute(html_names::kHiddenAttr);

  if (!RuntimeEnabledFeatures::BeforeMatchEventEnabled(GetExecutionContext())) {
    return MakeGarbageCollected<V8UnionBooleanOrStringOrUnrestrictedDouble>(
        attribute != g_null_atom);
  }

  if (attribute == g_null_atom) {
    return MakeGarbageCollected<V8UnionBooleanOrStringOrUnrestrictedDouble>(
        false);
  }
  if (attribute == "until-found") {
    return MakeGarbageCollected<V8UnionBooleanOrStringOrUnrestrictedDouble>(
        String("until-found"));
  }
  return MakeGarbageCollected<V8UnionBooleanOrStringOrUnrestrictedDouble>(true);
}

void HTMLElement::setHidden(
    const V8UnionBooleanOrStringOrUnrestrictedDouble* value) {
  if (!value) {
    removeAttribute(html_names::kHiddenAttr);
    return;
  }
  switch (value->GetContentType()) {
    case V8UnionBooleanOrStringOrUnrestrictedDouble::ContentType::kBoolean:
      if (value->GetAsBoolean()) {
        setAttribute(html_names::kHiddenAttr, "");
      } else {
        removeAttribute(html_names::kHiddenAttr);
      }
      break;
    case V8UnionBooleanOrStringOrUnrestrictedDouble::ContentType::kString:
      if (RuntimeEnabledFeatures::BeforeMatchEventEnabled(
              GetExecutionContext()) &&
          EqualIgnoringASCIICase(value->GetAsString(), "until-found")) {
        setAttribute(html_names::kHiddenAttr, "until-found");
      } else if (value->GetAsString() == "") {
        removeAttribute(html_names::kHiddenAttr);
      } else {
        setAttribute(html_names::kHiddenAttr, "");
      }
      break;
    case V8UnionBooleanOrStringOrUnrestrictedDouble::ContentType::
        kUnrestrictedDouble:
      double double_value = value->GetAsUnrestrictedDouble();
      if (double_value && !std::isnan(double_value)) {
        setAttribute(html_names::kHiddenAttr, "");
      } else {
        removeAttribute(html_names::kHiddenAttr);
      }
      break;
  }
}

namespace {
PopoverValueType GetPopoverTypeFromAttributeValue(String value) {
  if (EqualIgnoringASCIICase(value, kPopoverTypeValueAuto) ||
      (!value.IsNull() && value.empty())) {
    return PopoverValueType::kAuto;
  } else if (EqualIgnoringASCIICase(value, kPopoverTypeValueManual)) {
    return PopoverValueType::kManual;
  } else if (!value.IsNull()) {
    // Invalid values default to popover=manual.
    return PopoverValueType::kManual;
  }
  return PopoverValueType::kNone;
}
}  // namespace

void HTMLElement::UpdatePopoverAttribute(String value) {
  if (!RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
          GetDocument().GetExecutionContext())) {
    // If the feature flag isn't enabled, give a console warning about this
    // usage of the 'popover' attribute, which is likely to cause breakage when
    // the feature ships.
    auto& document = GetDocument();
    auto* console_message = MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kOther,
        mojom::blink::ConsoleMessageLevel::kError,
        "Found a 'popover' attribute. If you are testing the popover API, you "
        "must enable Experimental Web Platform Features. If not, note that "
        "custom attributes must start with 'data-': "
        "https://html.spec.whatwg.org/multipage/"
        "dom.html#custom-data-attribute. This usage will *likely cause site "
        "breakage* when the popover API ships: "
        "https://chromestatus.com/feature/5463833265045504.");
    console_message->SetNodes(document.GetFrame(),
                              {DOMNodeIds::IdForNode(this)});
    document.AddConsoleMessage(console_message);
    return;
  }

  PopoverValueType type = GetPopoverTypeFromAttributeValue(value);
  if (type == PopoverValueType::kManual &&
      !EqualIgnoringASCIICase(value, kPopoverTypeValueManual)) {
    // TODO(masonf) This console message might be too much log spam. Though
    // in case there's a namespace collision with something the developer is
    // doing with e.g. a function called 'popover', this will be helpful to
    // troubleshoot that.
    GetDocument().AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kOther,
        mojom::blink::ConsoleMessageLevel::kWarning,
        "Found a 'popover' attribute with an invalid value."));
  }
  if (HasPopoverAttribute()) {
    if (PopoverType() == type)
      return;
    String original_type = FastGetAttribute(html_names::kPopoverAttr);
    // If the popover type is changing, hide it.
    if (popoverOpen()) {
      HidePopoverInternal(HidePopoverFocusBehavior::kFocusPreviousElement,
                          HidePopoverForcingLevel::kHideAfterAnimations);
      // Event handlers could have changed the popover, including by removing
      // the popover attribute, or changing its value. If that happened, defer
      // to the change that already happened, and don't reset it again here.
      if (!isConnected() || !HasPopoverAttribute() ||
          original_type != FastGetAttribute(html_names::kPopoverAttr)) {
        return;
      }
    }
  }
  if (type == PopoverValueType::kNone) {
    if (HasPopoverAttribute()) {
      // If the popover attribute is being removed, remove the PopoverData.
      RemovePopoverData();
    }
    return;
  }
  UseCounter::Count(GetDocument(), WebFeature::kValidPopoverAttribute);
  DCHECK_EQ(type, GetPopoverTypeFromAttributeValue(
                      FastGetAttribute(html_names::kPopoverAttr)));
  EnsurePopoverData()->setType(type);
}

bool HTMLElement::HasPopoverAttribute() const {
  return GetPopoverData();
}

PopoverValueType HTMLElement::PopoverType() const {
  return GetPopoverData() ? GetPopoverData()->type() : PopoverValueType::kNone;
}

// This should be true when `:open` should match.
bool HTMLElement::popoverOpen() const {
  DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
      GetDocument().GetExecutionContext()));
  if (auto* popover_data = GetPopoverData())
    return popover_data->visibilityState() == PopoverVisibilityState::kShowing;
  return false;
}

void HTMLElement::togglePopover(ExceptionState& exception_state) {
  DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
      GetDocument().GetExecutionContext()));
  if (!HasPopoverAttribute()) {
    return exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "Not supported on elements that do not have a valid value for the "
        "'popover' attribute");
  } else if (!isConnected()) {
    return exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidStateError,
        "Invalid disconnected popover elements");
  }
  if (popoverOpen()) {
    hidePopover(exception_state);
  } else {
    showPopover(exception_state);
  }
}

// Showing a popover happens in phases, to facilitate animations and
// transitions:
// 1. Move the popover to the top layer, stop matching `:closed`, and
//     remove the UA `display:none` style.
// 2. Update style. (Transition initial style can be specified in this
//    state.)
// 3. Set the `:open` pseudo class.
// 4. Update style. (Animations/transitions happen here.)
void HTMLElement::showPopover(ExceptionState& exception_state) {
  DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
      GetDocument().GetExecutionContext()));
  if (!HasPopoverAttribute()) {
    return exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "Not supported on elements that do not have a valid value for the "
        "'popover' attribute");
  } else if (popoverOpen() || !isConnected()) {
    return exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidStateError,
        "Invalid on already-showing or disconnected popover elements");
  }

  // Fire the popovershow event (bubbles, cancelable).
  Event* event = Event::CreateCancelableBubble(event_type_names::kPopovershow);
  event->SetTarget(this);
  if (DispatchEvent(*event) != DispatchEventResult::kNotCanceled)
    return;

  // The 'popovershow' event handler could have changed this popover, e.g. by
  // changing its type, removing it from the document, or calling showPopover().
  if (!HasPopoverAttribute() || !isConnected() || popoverOpen())
    return;

  bool should_restore_focus = false;
  auto& document = GetDocument();
  auto original_type = PopoverType();
  if (original_type == PopoverValueType::kAuto) {
    // If the new popover is a popover=auto, hide any popover above this in the
    // stack. Because this popover isn't yet in the stack, we call
    // NearestOpenAncestralPopover to find this popover's ancestor, if any.
    const auto* auto_ancestor =
        NearestOpenAncestralPopover(*this, PopoverAncestorType::kNewPopover);
    HideAllPopoversUntil(auto_ancestor, document,
                         HidePopoverFocusBehavior::kNone,
                         HidePopoverForcingLevel::kHideAfterAnimations);

    // The 'popoverhide' event handlers could have changed this popover, e.g. by
    // changing its type, removing it from the document, or calling
    // showPopover().
    if (!HasPopoverAttribute() || !isConnected() || popoverOpen() ||
        PopoverType() != original_type)
      return;

    // We only restore focus for popover=auto, and only for the first popover in
    // the stack. If there's nothing showing, restore focus.
    should_restore_focus = !document.TopmostPopover();
    // Add this popover to the popover stack.
    auto& stack = document.PopoverStack();
    DCHECK(!stack.Contains(this));
    stack.push_back(this);
  }

  GetPopoverData()->setAnimationFinishedListener(nullptr);
  GetPopoverData()->setPreviouslyFocusedElement(nullptr);
  Element* originally_focused_element = document.FocusedElement();
  document.AddToTopLayer(this);
  // Stop matching `:closed`, and remove display:none styling:
  GetPopoverData()->setVisibilityState(PopoverVisibilityState::kTransitioning);
  PseudoStateChanged(CSSSelector::kPseudoClosed);

  // Force a style update. This ensures that base property values are set prior
  // to `:open` matching, so that transitions can start on the change to
  // top layer.
  document.UpdateStyleAndLayoutTreeForNode(this);
  EnsureComputedStyle();

  // Make the popover match `:open`:
  GetPopoverData()->setVisibilityState(PopoverVisibilityState::kShowing);
  PseudoStateChanged(CSSSelector::kPseudoOpen);

  SetPopoverFocusOnShow();

  // Only restore focus (later) if focus changed as a result of showing the
  // popover.
  if (should_restore_focus && HasPopoverAttribute() &&
      originally_focused_element != document.FocusedElement()) {
    GetPopoverData()->setPreviouslyFocusedElement(originally_focused_element);
  }
}

// static
// All popovers up to, but not including, |endpoint|, will be hidden.
void HTMLElement::HideAllPopoversUntil(const HTMLElement* endpoint,
                                       Document& document,
                                       HidePopoverFocusBehavior focus_behavior,
                                       HidePopoverForcingLevel forcing_level) {
  DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
      document.GetExecutionContext()));
  DCHECK(!endpoint || endpoint->HasPopoverAttribute());

  // If we're forcing a popover to hide immediately, first hide any other
  // popovers that have already started the hide process.
  if (forcing_level == HidePopoverForcingLevel::kHideImmediately) {
    auto popovers_to_hide = document.PopoversWaitingToHide();
    for (auto popover : popovers_to_hide)
      popover->PopoverHideFinishIfNeeded();
    DCHECK(document.PopoversWaitingToHide().empty());
  }

  auto close_all_open_popovers = [&document, &focus_behavior,
                                  &forcing_level]() {
    while (auto* popover = document.TopmostPopover()) {
      popover->HidePopoverInternal(focus_behavior, forcing_level);
    }
  };

  if (!endpoint)
    return close_all_open_popovers();

  DCHECK_EQ(endpoint->PopoverType(), PopoverValueType::kAuto);
  // Then hide everything in the popover=auto stack until the last_to_hide
  // popover is closed, or the stack is empty.
  const HTMLElement* last_to_hide = nullptr;
  bool found_endpoint = false;
  for (auto popover : document.PopoverStack()) {
    if (popover == endpoint) {
      found_endpoint = true;
    } else if (found_endpoint) {
      last_to_hide = popover;
      break;
    }
  }
  if (!found_endpoint)
    return close_all_open_popovers();
  while (last_to_hide && last_to_hide->popoverOpen() &&
         !document.PopoverStack().empty()) {
    document.PopoverStack().back()->HidePopoverInternal(focus_behavior,
                                                        forcing_level);
  }
}

void HTMLElement::hidePopover(ExceptionState& exception_state) {
  DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
      GetDocument().GetExecutionContext()));
  if (!HasPopoverAttribute()) {
    return exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "Not supported on elements that do not have a valid value for the "
        "'popover' attribute");
  } else if (GetPopoverData()->visibilityState() !=
             PopoverVisibilityState::kShowing) {
    // Important to check that visibility is not kShowing (rather than
    // popoverOpen()), because a hide transition might have been started on this
    // popover already, and we don't want to allow a double-hide.
    return exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidStateError,
        "Invalid on popover elements that aren't already showing");
  }
  HidePopoverInternal(HidePopoverFocusBehavior::kFocusPreviousElement,
                      HidePopoverForcingLevel::kHideAfterAnimations);
}

// Hiding a popover happens in phases, to facilitate animations and
// transitions:
// 1. Capture any already-running animations via getAnimations(), including
//    animations on descendant elements.
// 2. Remove the `:open` pseudo class.
// 3. Fire the 'popoverhide' event.
// 4. If the hidePopover() call is *not* the result of the popover being "forced
//    out" of the top layer, e.g. by a modal dialog or fullscreen element:
//   a. Restore focus to the previously-focused element.
//   b. Update style. (Animations/transitions start here.)
//   c. Call getAnimations() again, remove any from step #1, and then wait
//      until all of them finish or are cancelled.
// 5. Remove the popover from the top layer, and add the UA display:none style.
// 6. Update style.
void HTMLElement::HidePopoverInternal(HidePopoverFocusBehavior focus_behavior,
                                      HidePopoverForcingLevel forcing_level) {
  DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
      GetDocument().GetExecutionContext()));
  DCHECK(HasPopoverAttribute());
  auto& document = GetDocument();
  auto original_type = PopoverType();
  if (original_type == PopoverValueType::kAuto) {
    // Hide any popovers above us in the stack.
    HideAllPopoversUntil(this, document, focus_behavior, forcing_level);

    // The 'popoverhide' event handlers could have changed this popover, e.g. by
    // changing its type, removing it from the document, or calling
    // hidePopover().
    if (!HasPopoverAttribute() || !isConnected() ||
        GetPopoverData()->visibilityState() !=
            PopoverVisibilityState::kShowing ||
        PopoverType() != original_type) {
      DCHECK(!GetDocument().PopoverStack().Contains(this));
      return;
    }

    // Then remove this popover from the stack, if present. If the popover
    // is already hidden, it won't be in the stack.
    auto& stack = document.PopoverStack();
    DCHECK(!stack.empty());
    DCHECK_EQ(stack.back(), this);
    stack.pop_back();
  }
  document.PopoversWaitingToHide().insert(this);

  bool force_hide = forcing_level == HidePopoverForcingLevel::kHideImmediately;
  HeapVector<Member<Animation>> previous_animations;
  if (!force_hide) {
    previous_animations = GetAnimationsInternal(
        GetAnimationsOptionsResolved{.use_subtree = true});
  }

  GetPopoverData()->setInvoker(nullptr);
  GetPopoverData()->setNeedsRepositioningForSelectMenu(false);
  // Stop matching `:open`:
  GetPopoverData()->setVisibilityState(PopoverVisibilityState::kTransitioning);
  PseudoStateChanged(CSSSelector::kPseudoOpen);

  // Fire the popoverhide event (bubbles, not cancelable).
  Event* event = Event::CreateBubble(event_type_names::kPopoverhide);
  event->SetTarget(this);
  if (force_hide) {
    // We will be force-hidden when the popover element is being removed from
    // the document, during which event dispatch is prohibited.
    GetDocument().EnqueueAnimationFrameEvent(event);
    // Immediately finish the hide process.
    return PopoverHideFinishIfNeeded();
  }
  auto result = DispatchEvent(*event);
  DCHECK_EQ(result, DispatchEventResult::kNotCanceled);

  // The 'popoverhide' event handler could have changed this popover, e.g. by
  // changing its type, removing it from the document, or calling showPopover().
  if (!isConnected() || !HasPopoverAttribute() ||
      GetPopoverData()->visibilityState() !=
          PopoverVisibilityState::kTransitioning) {
    return;
  }

  // Grab all animations, so that we can "finish" the hide operation once
  // they complete. This will *also* force a style update, ensuring property
  // values are set after `:open` stops matching, so that transitions
  // can start.
  HeapHashSet<Member<EventTarget>> animations;
  for (const auto& animation : GetAnimationsInternal(
           GetAnimationsOptionsResolved{.use_subtree = true})) {
    animations.insert(animation);
  }
  animations.RemoveAll(previous_animations);
  if (animations.empty()) {
    // No animations to wait for: just finish immediately.
    PopoverHideFinishIfNeeded();
  } else {
    GetPopoverData()->setAnimationFinishedListener(
        MakeGarbageCollected<PopoverAnimationFinishedEventListener>(
            this, std::move(animations)));
  }

  Element* previously_focused_element =
      GetPopoverData()->previouslyFocusedElement();
  if (previously_focused_element) {
    GetPopoverData()->setPreviouslyFocusedElement(nullptr);
    if (focus_behavior == HidePopoverFocusBehavior::kFocusPreviousElement) {
      FocusOptions* focus_options = FocusOptions::Create();
      focus_options->setPreventScroll(true);
      previously_focused_element->Focus(focus_options);
    }
  }
}

void HTMLElement::PopoverHideFinishIfNeeded() {
  DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
      GetDocument().GetExecutionContext()));
  GetDocument().PopoversWaitingToHide().erase(this);
  GetDocument().RemoveFromTopLayer(this);
  // Re-apply display:none, and start matching `:closed`.
  if (GetPopoverData()) {
    GetPopoverData()->setVisibilityState(PopoverVisibilityState::kHidden);
    GetPopoverData()->setAnimationFinishedListener(nullptr);
    PseudoStateChanged(CSSSelector::kPseudoClosed);
  }
}

Element* HTMLElement::GetPopoverFirstFocusableElement(bool autofocus_only) {
  // If the popover has autofocus, focus it.
  if (IsAutofocusable())
    return this;
  // Otherwise, look for a child control that has the autofocus attribute.
  return GetPopoverFocusableArea(autofocus_only);
}

void HTMLElement::SetPopoverFocusOnShow() {
  DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
      GetDocument().GetExecutionContext()));
  // The layout must be updated here because we call Element::isFocusable,
  // which requires an up-to-date layout.
  GetDocument().UpdateStyleAndLayoutTreeForNode(this);

  Element* control = GetPopoverFirstFocusableElement(/*autofocus_only*/ true);

  // If the popover does not use autofocus, then the focus should remain on the
  // currently active element.
  // https://open-ui.org/components/popup.research.explainer#focus-management
  if (!control)
    return;

  // 3. Run the focusing steps for control.
  control->Focus();

  // 4. Let topDocument be the active document of control's node document's
  // browsing context's top-level browsing context.
  // 5. If control's node document's origin is not the same as the origin of
  // topDocument, then return.
  Document& doc = control->GetDocument();
  if (!doc.IsActive())
    return;
  if (!doc.IsInMainFrame() &&
      !doc.TopFrameOrigin()->CanAccess(
          doc.GetExecutionContext()->GetSecurityOrigin())) {
    return;
  }

  // 6. Empty topDocument's autofocus candidates.
  // 7. Set topDocument's autofocus processed flag to true.
  doc.TopDocument().FinalizeAutofocus();
}

// TODO(masonf) This should really be combined with Element::GetFocusableArea(),
// and can possibly be merged with the similar logic for <dialog>. The spec for
// https://html.spec.whatwg.org/multipage/interaction.html#get-the-focusable-area
// does not include dialogs or popovers yet.
Element* HTMLElement::GetPopoverFocusableArea(bool autofocus_only) const {
  DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
      GetDocument().GetExecutionContext()));
  Node* next = nullptr;
  for (Node* node = FlatTreeTraversal::FirstChild(*this); node; node = next) {
    next = FlatTreeTraversal::Next(*node, this);
    auto* element = DynamicTo<HTMLElement>(node);
    if (!element)
      continue;
    if (element->HasPopoverAttribute() || IsA<HTMLDialogElement>(*element)) {
      next = FlatTreeTraversal::NextSkippingChildren(*element, this);
      continue;
    }
    if (element->IsFocusable() &&
        (!autofocus_only || element->IsAutofocusable())) {
      return element;
    }
  }
  return nullptr;
}

using PopoverPositionMap = HeapHashMap<Member<const Element>, int>;
using PopoverAnchorMap =
    HeapHashMap<Member<const Element>, Member<const Element>>;
using PopoverSeenSet = HashSet<Member<const Node>>;

namespace {
const HTMLElement* NearestOpenAncestralPopoverRecursive(
    const Node* node,
    const PopoverPositionMap& popover_positions,
    const PopoverAnchorMap& anchors_to_popovers,
    int upper_bound,
    PopoverSeenSet& seen) {
  if (!node || seen.Contains(node))
    return nullptr;
  seen.insert(node);

  const HTMLElement* ancestor = nullptr;
  int position = -1;
  auto update = [&ancestor, &position, &popover_positions,
                 upper_bound](const HTMLElement* popover) {
    if (popover && popover->popoverOpen() &&
        popover->PopoverType() != PopoverValueType::kManual) {
      DCHECK(popover_positions.Contains(popover));
      int new_position = popover_positions.at(popover);
      if (new_position > position && new_position < upper_bound) {
        ancestor = popover;
        position = new_position;
      }
    }
  };
  auto recurse_and_update = [&update, &popover_positions, upper_bound,
                             &anchors_to_popovers, &seen](const Node* node) {
    update(NearestOpenAncestralPopoverRecursive(
        node, popover_positions, anchors_to_popovers, upper_bound, seen));
  };

  if (auto* element = DynamicTo<HTMLElement>(node)) {
    // Update for this element.
    update(element);
    // Recursively look up the tree from this element's anchors and invokers.
    if (popover_positions.Contains(element)) {
      recurse_and_update(element->anchorElement());
      recurse_and_update(element->GetPopoverData()->invoker());
    }
    // Include invokers that weren't used to invoke the popover. This is
    // necessary to catch invoking elements that should not light dismiss a
    // popover, even if they weren't used to show it.
    if (auto* form_control = DynamicTo<HTMLFormControlElement>(element)) {
      recurse_and_update(form_control->popoverTargetElement().element);
    }
    // Include the anchor elements for all showing popovers.
    if (anchors_to_popovers.Contains(element)) {
      recurse_and_update(anchors_to_popovers.at(element));
    }
  }
  // Also walk up the flat tree from this node.
  recurse_and_update(FlatTreeTraversal::Parent(*node));

  return ancestor;
}
}  // namespace

// static
// This function will return the popover that is highest in the popover stack
// that is an ancestral popover of the provided node. Popover ancestors are
// created by DOM flat tree parents, or through either anchor or invoker
// relationships. Anchor relationships are formed by the anchor attribute on a
// popover, pointing to another node in the tree. Invoker relationships are
// formed by invoking elements, which are HTMLFormControlElements having
// popovertoggletarget, popovershowtarget, or popoverhidetarget attributes
// pointing to a popover element. There can be multiple popovers that point to a
// single anchor element, and there can be multiple invoking elements for a
// single popover. Additionally, an anchor for one popover can be an invoker for
// a different popover. For these reasons, this function needs to do a recursive
// tree walk up from the provided node, plus all associated anchors and
// invokers, returning the highest (on the stack) popover that is found. If the
// inclusive parameter is true, the highest popover found during the tree-walk
// is included in the search. If it is false, the |node| parameter must be a
// popover, and the highest popover *below* that starting pop- up will be
// returned.
const HTMLElement* HTMLElement::NearestOpenAncestralPopover(
    const Node& node,
    PopoverAncestorType ancestor_type) {
  DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
      node.GetDocument().GetExecutionContext()));
  // popover_positions is a map from all showing (or about-to-show) popovers to
  // their position in the popover stack.
  PopoverPositionMap popover_positions;
  // anchors_to_popovers is a map from the anchor elements of all showing
  // popovers back to the popover itself.
  PopoverAnchorMap anchors_to_popovers;
  int indx = 0;
  for (auto popover : node.GetDocument().PopoverStack()) {
    popover_positions.Set(popover, indx++);
    if (popover->anchorElement())
      anchors_to_popovers.Set(popover->anchorElement(), popover);
  }
  auto* element = DynamicTo<HTMLElement>(node);
  if (ancestor_type == PopoverAncestorType::kNewPopover) {
    DCHECK(element && element->HasPopoverAttribute() &&
           !element->popoverOpen());
    popover_positions.Set(element, indx++);
  }
  // upper_bound is one above the maximum popover stack height to accept. It is
  // typically the position of the provided element.
  int upper_bound = popover_positions.Contains(element)
                        ? popover_positions.at(element)
                        : INT_MAX;
  if (ancestor_type == PopoverAncestorType::kInclusive) {
    // For inclusive mode, we need to walk up the tree until we find an open
    // popover, or an invoker for an open popover, and then modify the upper
    // bound to include the highest such popover found, if any.
    for (const Node* current_node = &node; current_node;
         current_node = FlatTreeTraversal::Parent(*current_node)) {
      if (auto* current_element = DynamicTo<HTMLElement>(current_node);
          current_element && current_element->HasPopoverAttribute() &&
          current_element->popoverOpen() &&
          current_element->PopoverType() != PopoverValueType::kManual) {
        upper_bound =
            std::max(upper_bound, popover_positions.at(current_element) + 1);
      }
      if (auto* form_control =
              DynamicTo<HTMLFormControlElement>(current_node)) {
        if (auto target_popover = form_control->popoverTargetElement().element;
            target_popover && target_popover->popoverOpen() &&
            target_popover->PopoverType() != PopoverValueType::kManual) {
          upper_bound =
              std::max(upper_bound, popover_positions.at(target_popover) + 1);
        }
      }
    }
  }
  PopoverSeenSet seen;
  return NearestOpenAncestralPopoverRecursive(
      &node, popover_positions, anchors_to_popovers, upper_bound, seen);
}

// static
void HTMLElement::HandlePopoverLightDismiss(const Event& event) {
  if (event.GetEventPath().IsEmpty())
    return;
  DCHECK_NE(Event::PhaseType::kNone, event.eventPhase());
  if (event.eventPhase() == Event::PhaseType::kBubblingPhase)
    return;
  if (!event.isTrusted())
    return;
  // Ensure that shadow DOM event retargeting is considered when computing
  // the event target node.
  auto* target_node = event.GetEventPath()[0].Target()->ToNode();
  if (!target_node)
    return;
  auto& document = target_node->GetDocument();
  DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
      document.GetExecutionContext()));
  DCHECK(document.TopmostPopover());
  const AtomicString& event_type = event.type();
  if (IsA<PointerEvent>(event)) {
    if (event_type == event_type_names::kPointerdown) {
      document.SetPopoverPointerdownTarget(NearestOpenAncestralPopover(
          *target_node, PopoverAncestorType::kInclusive));
    } else if (event_type == event_type_names::kPointerup) {
      // Hide everything up to the clicked element. We do this on pointerup,
      // rather than pointerdown or click, primarily for accessibility concerns.
      // See
      // https://www.w3.org/WAI/WCAG21/Understanding/pointer-cancellation.html
      // for more information on why it is better to perform potentially
      // destructive actions (including hiding a popover) on pointer-up rather
      // than pointer-down. To properly handle the use case where a user starts
      // a pointer-drag on a popover, and finishes off the popover (to highlight
      // text), the ancestral popover is stored in pointerdown and compared
      // here.
      auto* ancestor_popover = NearestOpenAncestralPopover(
          *target_node, PopoverAncestorType::kInclusive);
      bool same_target =
          ancestor_popover == document.PopoverPointerdownTarget();
      document.SetPopoverPointerdownTarget(nullptr);
      if (same_target) {
        HideAllPopoversUntil(ancestor_popover, document,
                             HidePopoverFocusBehavior::kNone,
                             HidePopoverForcingLevel::kHideAfterAnimations);
      }
    }
  } else if (event_type == event_type_names::kKeydown) {
    const KeyboardEvent* key_event = DynamicTo<KeyboardEvent>(event);
    if (key_event && key_event->key() == "Escape") {
      // Escape key just pops the topmost popover off the stack.
      document.TopmostPopover()->HidePopoverInternal(
          HidePopoverFocusBehavior::kFocusPreviousElement,
          HidePopoverForcingLevel::kHideAfterAnimations);
    }
  }
}

void HTMLElement::InvokePopover(Element* invoker) {
  DCHECK(invoker);
  DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
      GetDocument().GetExecutionContext()));
  DCHECK(HasPopoverAttribute());
  GetPopoverData()->setInvoker(invoker);
  showPopover(ASSERT_NO_EXCEPTION);
}

Element* HTMLElement::anchorElement() const {
  if (!RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
          GetDocument().GetExecutionContext()))
    return nullptr;
  if (!HasPopoverAttribute())
    return nullptr;
  const AtomicString& anchor_id = FastGetAttribute(html_names::kAnchorAttr);
  if (anchor_id.IsNull())
    return nullptr;
  if (!IsInTreeScope())
    return nullptr;
  return GetTreeScope().getElementById(anchor_id);  // may be null
}

void HTMLElement::SetNeedsRepositioningForSelectMenu(bool flag) {
  DCHECK(RuntimeEnabledFeatures::HTMLSelectMenuElementEnabled());
  DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
      GetDocument().GetExecutionContext()));
  DCHECK(HasPopoverAttribute());
  auto* popover_data = GetPopoverData();
  if (popover_data->needsRepositioningForSelectMenu() == flag)
    return;
  popover_data->setNeedsRepositioningForSelectMenu(flag);
  if (flag) {
    SetHasCustomStyleCallbacks();
    SetNeedsStyleRecalc(kLocalStyleChange,
                        StyleChangeReasonForTracing::Create(
                            style_change_reason::kPopoverVisibilityChange));
  }
}

void HTMLElement::SetOwnerSelectMenuElement(HTMLSelectMenuElement* element) {
  DCHECK(RuntimeEnabledFeatures::HTMLSelectMenuElementEnabled());
  DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
      GetDocument().GetExecutionContext()));
  DCHECK(HasPopoverAttribute());
  GetPopoverData()->setOwnerSelectMenuElement(element);
}

// TODO(crbug.com/1197720): The popover position should be provided by the new
// anchored positioning scheme.
scoped_refptr<ComputedStyle> HTMLElement::StyleForSelectMenuPopoverstyle(
    const StyleRecalcContext& style_recalc_context) {
  DCHECK(RuntimeEnabledFeatures::HTMLSelectMenuElementEnabled());
  DCHECK(HasPopoverAttribute());
  DCHECK(GetPopoverData()->needsRepositioningForSelectMenu());
  auto* owner_select = GetPopoverData()->ownerSelectMenuElement();
  DCHECK(owner_select);

  scoped_refptr<ComputedStyle> original_style =
      OriginalStyleForLayoutObject(style_recalc_context);

  LocalDOMWindow* window = GetDocument().domWindow();
  if (!window)
    return original_style;

  ComputedStyleBuilder style_builder(*original_style);

  gfx::RectF anchor_rect_in_screen =
      owner_select->GetBoundingClientRectNoLifecycleUpdate();
  const float anchor_zoom =
      owner_select->GetLayoutObject()
          ? owner_select->GetLayoutObject()->StyleRef().EffectiveZoom()
          : 1;
  anchor_rect_in_screen.Scale(anchor_zoom);
  // Don't use the LocalDOMWindow innerHeight/innerWidth getters, as those can
  // trigger a re-entrant style and layout update.
  int avail_width = GetDocument().View()->Size().width();
  int avail_height = GetDocument().View()->Size().height();
  gfx::Rect avail_rect = gfx::Rect(0, 0, avail_width, avail_height);

  // Remove any margins on the listbox part, so we can position it correctly.
  style_builder.SetMarginTop(Length::Fixed(0));
  style_builder.SetMarginLeft(Length::Fixed(0));
  style_builder.SetMarginRight(Length::Fixed(0));
  style_builder.SetMarginBottom(Length::Fixed(0));

  // Position the listbox part where is more space available.
  const float available_space_above =
      anchor_rect_in_screen.y() - avail_rect.y();
  const float available_space_below =
      avail_rect.bottom() - anchor_rect_in_screen.bottom();
  if (available_space_below < available_space_above) {
    style_builder.SetMaxHeight(Length::Fixed(available_space_above));
    style_builder.SetBottom(
        Length::Fixed(avail_rect.bottom() - anchor_rect_in_screen.y()));
    style_builder.SetTop(Length::Auto());
  } else {
    style_builder.SetMaxHeight(Length::Fixed(available_space_below));
    style_builder.SetTop(Length::Fixed(anchor_rect_in_screen.bottom()));
  }

  const float available_space_if_left_anchored =
      avail_rect.right() - anchor_rect_in_screen.x();
  const float available_space_if_right_anchored =
      anchor_rect_in_screen.right() - avail_rect.x();
  style_builder.SetMinWidth(Length::Fixed(anchor_rect_in_screen.width()));
  if (available_space_if_left_anchored > anchor_rect_in_screen.width() ||
      available_space_if_left_anchored > available_space_if_right_anchored) {
    style_builder.SetLeft(Length::Fixed(anchor_rect_in_screen.x()));
    style_builder.SetMaxWidth(Length::Fixed(available_space_if_left_anchored));
  } else {
    style_builder.SetRight(
        Length::Fixed(avail_rect.right() - anchor_rect_in_screen.right()));
    style_builder.SetLeft(Length::Auto());
    style_builder.SetMaxWidth(Length::Fixed(available_space_if_right_anchored));
  }

  return style_builder.TakeStyle();
}

scoped_refptr<ComputedStyle> HTMLElement::CustomStyleForLayoutObject(
    const StyleRecalcContext& style_recalc_context) {
  DCHECK(HasCustomStyleCallbacks());
  // TODO(crbug.com/1197720): This logic is for positioning the selectmenu
  // popover. This should be replaced by the new anchored positioning scheme.
  if (HasPopoverAttribute() &&
      GetPopoverData()->needsRepositioningForSelectMenu()) {
    DCHECK(RuntimeEnabledFeatures::HTMLSelectMenuElementEnabled());
    DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
        GetDocument().GetExecutionContext()));
    return StyleForSelectMenuPopoverstyle(style_recalc_context);
  }
  return Element::CustomStyleForLayoutObject(style_recalc_context);
}

bool HTMLElement::DispatchFocusEvent(
    Element* old_focused_element,
    mojom::blink::FocusType type,
    InputDeviceCapabilities* source_capabilities) {
  return Element::DispatchFocusEvent(old_focused_element, type,
                                     source_capabilities);
}

const AtomicString& HTMLElement::autocapitalize() const {
  DEFINE_STATIC_LOCAL(const AtomicString, kOff, ("off"));
  DEFINE_STATIC_LOCAL(const AtomicString, kNone, ("none"));
  DEFINE_STATIC_LOCAL(const AtomicString, kCharacters, ("characters"));
  DEFINE_STATIC_LOCAL(const AtomicString, kWords, ("words"));
  DEFINE_STATIC_LOCAL(const AtomicString, kSentences, ("sentences"));

  const AtomicString& value = FastGetAttribute(html_names::kAutocapitalizeAttr);
  if (value.empty())
    return g_empty_atom;

  if (EqualIgnoringASCIICase(value, kNone) ||
      EqualIgnoringASCIICase(value, kOff))
    return kNone;
  if (EqualIgnoringASCIICase(value, kCharacters))
    return kCharacters;
  if (EqualIgnoringASCIICase(value, kWords))
    return kWords;
  // "sentences", "on", or an invalid value
  return kSentences;
}

void HTMLElement::setAutocapitalize(const AtomicString& value) {
  setAttribute(html_names::kAutocapitalizeAttr, value);
}

bool HTMLElement::isContentEditableForBinding() const {
  return IsEditableOrEditingHost(*this);
}

bool HTMLElement::draggable() const {
  return EqualIgnoringASCIICase(FastGetAttribute(html_names::kDraggableAttr),
                                "true");
}

void HTMLElement::setDraggable(bool value) {
  setAttribute(html_names::kDraggableAttr, value ? "true" : "false");
}

bool HTMLElement::spellcheck() const {
  return IsSpellCheckingEnabled();
}

void HTMLElement::setSpellcheck(bool enable) {
  setAttribute(html_names::kSpellcheckAttr, enable ? "true" : "false");
}

void HTMLElement::click() {
  DispatchSimulatedClick(nullptr, SimulatedClickCreationScope::kFromScript);
  if (IsA<HTMLInputElement>(this)) {
    UseCounter::Count(GetDocument(),
                      WebFeature::kHTMLInputElementSimulatedClick);
  }
}

void HTMLElement::AccessKeyAction(SimulatedClickCreationScope creation_scope) {
  DispatchSimulatedClick(nullptr, creation_scope);
}

String HTMLElement::title() const {
  return FastGetAttribute(html_names::kTitleAttr);
}

TranslateAttributeMode HTMLElement::GetTranslateAttributeMode() const {
  const AtomicString& value = FastGetAttribute(html_names::kTranslateAttr);

  if (value == g_null_atom)
    return kTranslateAttributeInherit;
  if (EqualIgnoringASCIICase(value, "yes") || EqualIgnoringASCIICase(value, ""))
    return kTranslateAttributeYes;
  if (EqualIgnoringASCIICase(value, "no"))
    return kTranslateAttributeNo;

  return kTranslateAttributeInherit;
}

bool HTMLElement::translate() const {
  for (const HTMLElement* element = this; element;
       element = Traversal<HTMLElement>::FirstAncestor(*element)) {
    TranslateAttributeMode mode = element->GetTranslateAttributeMode();
    if (mode != kTranslateAttributeInherit) {
      DCHECK(mode == kTranslateAttributeYes || mode == kTranslateAttributeNo);
      return mode == kTranslateAttributeYes;
    }
  }

  // Default on the root element is translate=yes.
  return true;
}

void HTMLElement::setTranslate(bool enable) {
  setAttribute(html_names::kTranslateAttr, enable ? "yes" : "no");
}

// Returns the conforming 'dir' value associated with the state the attribute is
// in (in its canonical case), if any, or the empty string if the attribute is
// in a state that has no associated keyword value or if the attribute is not in
// a defined state (e.g. the attribute is missing and there is no missing value
// default).
// http://www.whatwg.org/specs/web-apps/current-work/multipage/common-dom-interfaces.html#limited-to-only-known-values
static inline const AtomicString& ToValidDirValue(const AtomicString& value) {
  DEFINE_STATIC_LOCAL(const AtomicString, ltr_value, ("ltr"));
  DEFINE_STATIC_LOCAL(const AtomicString, rtl_value, ("rtl"));
  DEFINE_STATIC_LOCAL(const AtomicString, auto_value, ("auto"));

  if (EqualIgnoringASCIICase(value, ltr_value))
    return ltr_value;
  if (EqualIgnoringASCIICase(value, rtl_value))
    return rtl_value;
  if (EqualIgnoringASCIICase(value, auto_value))
    return auto_value;
  return g_null_atom;
}

const AtomicString& HTMLElement::dir() {
  return ToValidDirValue(FastGetAttribute(html_names::kDirAttr));
}

void HTMLElement::setDir(const AtomicString& value) {
  setAttribute(html_names::kDirAttr, value);
}

HTMLFormElement* HTMLElement::formOwner() const {
  if (const auto* internals = GetElementInternals())
    return internals->Form();
  return nullptr;
}

HTMLFormElement* HTMLElement::FindFormAncestor() const {
  return Traversal<HTMLFormElement>::FirstAncestor(*this);
}

static inline bool ElementAffectsDirectionality(const Node* node) {
  auto* html_element = DynamicTo<HTMLElement>(node);
  return html_element && (IsA<HTMLBDIElement>(*html_element) ||
                          IsValidDirAttribute(html_element->FastGetAttribute(
                              html_names::kDirAttr)));
}

void HTMLElement::ChildrenChanged(const ChildrenChange& change) {
  Element::ChildrenChanged(change);

  if (HasDirectionAuto()) {
    SetSelfOrAncestorHasDirAutoAttribute();
    GetDocument().SetDirAttributeDirty();
  }

  if (GetDocument().IsDirAttributeDirty()) {
    AdjustDirectionalityIfNeededAfterChildrenChanged(change);

    if (change.IsChildInsertion() && !SelfOrAncestorHasDirAutoAttribute()) {
      auto* element = DynamicTo<HTMLElement>(change.sibling_changed);
      if (element && !element->NeedsInheritDirectionalityFromParent() &&
          !ElementAffectsDirectionality(element))
        element->UpdateDirectionalityAndDescendant(CachedDirectionality());
    }
  }
  if (change.IsChildInsertion()) {
    CheckSoftNavigationHeuristicsTracking(GetDocument(),
                                          change.sibling_changed);
  }
}

bool HTMLElement::HasDirectionAuto() const {
  // <bdi> defaults to dir="auto"
  // https://html.spec.whatwg.org/C/#the-bdi-element
  const AtomicString& direction = FastGetAttribute(html_names::kDirAttr);
  return (IsA<HTMLBDIElement>(*this) && direction == g_null_atom) ||
         EqualIgnoringASCIICase(direction, "auto");
}

template <typename Traversal>
absl::optional<TextDirection> HTMLElement::ResolveAutoDirectionality(
    bool& is_deferred,
    Node* stay_within) const {
  is_deferred = false;
  if (auto* input_element = DynamicTo<HTMLInputElement>(*this)) {
    bool has_strong_directionality;
    return DetermineDirectionality(input_element->Value(),
                                   &has_strong_directionality);
  }

  // For <textarea>, the heuristic is applied on a per-paragraph level, and
  // we should traverse the flat tree.
  Node* node = (IsA<HTMLTextAreaElement>(*this) || IsA<HTMLSlotElement>(*this))
                   ? FlatTreeTraversal::FirstChild(*this)
                   : Traversal::FirstChild(*this);
  while (node) {
    // Skip bdi, script, style and text form controls.
    auto* element = DynamicTo<Element>(node);
    if (EqualIgnoringASCIICase(node->nodeName(), "bdi") ||
        IsA<HTMLScriptElement>(*node) || IsA<HTMLStyleElement>(*node) ||
        (element && element->IsTextControl()) ||
        (element && element->ShadowPseudoId() ==
                        shadow_element_names::kPseudoInputPlaceholder)) {
      node = Traversal::NextSkippingChildren(*node, stay_within);
      continue;
    }

    auto* slot = ToHTMLSlotElementIfSupportsAssignmentOrNull(node);
    if (slot) {
      ShadowRoot* root = slot->ContainingShadowRoot();
      // Defer to adjust the directionality to avoid recalcuating slot
      // assignment in FlatTreeTraversal when updating slot.
      // ResolveAutoDirectionality will be adjusted after recalculating its
      // children.
      if (root->NeedsSlotAssignmentRecalc()) {
        is_deferred = true;
        return TextDirection::kLtr;
      }
    }

    // Skip elements with valid dir attribute
    if (auto* element_node = DynamicTo<Element>(node)) {
      AtomicString dir_attribute_value =
          element_node->FastGetAttribute(html_names::kDirAttr);
      if (IsValidDirAttribute(dir_attribute_value)) {
        node = Traversal::NextSkippingChildren(*node, stay_within);
        continue;
      }
    }

    if (node->IsTextNode()) {
      bool has_strong_directionality;
      TextDirection text_direction = DetermineDirectionality(
          node->textContent(true), &has_strong_directionality);
      if (has_strong_directionality)
        return text_direction;
    }

    if (slot) {
      absl::optional<TextDirection> text_direction =
          slot->ResolveAutoDirectionality<FlatTreeTraversal>(is_deferred,
                                                             stay_within);
      if (text_direction.has_value())
        return text_direction;
    }

    node = Traversal::Next(*node, stay_within);
  }
  return absl::nullopt;
}

void HTMLElement::AdjustDirectionalityIfNeededAfterChildAttributeChanged(
    Element* child) {
  DCHECK(SelfOrAncestorHasDirAutoAttribute());
  bool is_deferred;
  TextDirection text_direction =
      ResolveAutoDirectionality<NodeTraversal>(is_deferred, this)
          .value_or(TextDirection::kLtr);
  if (CachedDirectionality() != text_direction && !is_deferred) {
    SetCachedDirectionality(text_direction);

    for (Element* element_to_adjust = this; element_to_adjust;
         element_to_adjust =
             FlatTreeTraversal::ParentElement(*element_to_adjust)) {
      if (ElementAffectsDirectionality(element_to_adjust)) {
        DynamicTo<HTMLElement>(element_to_adjust)
            ->UpdateDirectionalityAndDescendant(text_direction);

        const ComputedStyle* style = GetComputedStyle();
        if (style && style->Direction() != text_direction) {
          element_to_adjust->SetNeedsStyleRecalc(
              kLocalStyleChange, StyleChangeReasonForTracing::Create(
                                     style_change_reason::kWritingModeChange));
        }
        return;
      }
    }
  }
}

bool HTMLElement::CalculateAndAdjustAutoDirectionality(Node* stay_within) {
  bool is_deferred = false;
  TextDirection text_direction =
      ResolveAutoDirectionality<NodeTraversal>(is_deferred, stay_within)
          .value_or(TextDirection::kLtr);
  if (CachedDirectionality() != text_direction && !is_deferred) {
    UpdateDirectionalityAndDescendant(text_direction);

    const ComputedStyle* style = GetComputedStyle();
    if (style && style->Direction() != text_direction) {
      SetNeedsStyleRecalc(kLocalStyleChange,
                          StyleChangeReasonForTracing::Create(
                              style_change_reason::kWritingModeChange));
      return true;
    }
  }

  return false;
}

void HTMLElement::AdjustDirectionalityIfNeededAfterChildrenChanged(
    const ChildrenChange& change) {
  if (!SelfOrAncestorHasDirAutoAttribute())
    return;

  Node* stay_within = nullptr;
  bool has_strong_directionality;
  if (change.type == ChildrenChangeType::kTextChanged) {
    TextDirection old_text_direction =
        DetermineDirectionality(change.old_text, &has_strong_directionality);
    auto* character_data = DynamicTo<CharacterData>(change.sibling_changed);
    DCHECK(character_data);
    TextDirection new_text_direction = DetermineDirectionality(
        character_data->data(), &has_strong_directionality);
    if (old_text_direction == new_text_direction)
      return;
    stay_within = change.sibling_changed;
  } else if (change.IsChildInsertion()) {
    if (change.sibling_changed->IsTextNode()) {
      TextDirection new_text_direction =
          DetermineDirectionality(change.sibling_changed->textContent(true),
                                  &has_strong_directionality);
      if (!has_strong_directionality ||
          new_text_direction == CachedDirectionality())
        return;
    }
    stay_within = change.sibling_changed;
  }

  UpdateDescendantHasDirAutoAttribute(true /* has_dir_auto */);

  for (Element* element_to_adjust = this; element_to_adjust;
       element_to_adjust =
           FlatTreeTraversal::ParentElement(*element_to_adjust)) {
    if (ElementAffectsDirectionality(element_to_adjust)) {
      if (To<HTMLElement>(element_to_adjust)
              ->CalculateAndAdjustAutoDirectionality(
                  stay_within ? stay_within : element_to_adjust)) {
        SetNeedsStyleRecalc(kLocalStyleChange,
                            StyleChangeReasonForTracing::Create(
                                style_change_reason::kPseudoClass));
      }
      if (RuntimeEnabledFeatures::CSSPseudoDirEnabled())
        element_to_adjust->PseudoStateChanged(CSSSelector::kPseudoDir);
      return;
    }
  }
}

void HTMLElement::AdjustDirectionalityIfNeededAfterShadowRootChanged() {
  DCHECK(IsShadowHost(this));
  if (SelfOrAncestorHasDirAutoAttribute()) {
    for (auto* element_to_adjust = this; element_to_adjust;
         element_to_adjust = DynamicTo<HTMLElement>(
             FlatTreeTraversal::ParentElement(*element_to_adjust))) {
      if (ElementAffectsDirectionality(element_to_adjust)) {
        element_to_adjust->CalculateAndAdjustAutoDirectionality(
            element_to_adjust);
        return;
      }
    }
  } else if (!NeedsInheritDirectionalityFromParent()) {
    UpdateDescendantDirectionality(CachedDirectionality());
  }
}

void HTMLElement::AdjustCandidateDirectionalityForSlot(
    HeapHashSet<Member<Node>> candidate_set) {
  HeapHashSet<Member<HTMLElement>> directionality_set;
  // Transfer a candidate directionality set to |directionality_set| to avoid
  // the tree walk to the duplicated parent node for the directionality.
  for (auto& node : candidate_set) {
    Node* node_to_adjust = node.Get();
    if (!node->SelfOrAncestorHasDirAutoAttribute()) {
      if (ElementAffectsDirectionality(node))
        continue;
      auto* slot = node->AssignedSlot();
      if (slot && slot->SelfOrAncestorHasDirAutoAttribute()) {
        node_to_adjust = slot;
      } else {
        if (slot && !slot->NeedsInheritDirectionalityFromParent()) {
          node->SetCachedDirectionality(slot->CachedDirectionality());
        }
        continue;
      }
    }

    bool needs_slot_assignment_recalc = false;
    for (auto* element_to_adjust = DynamicTo<HTMLElement>(node_to_adjust);
         element_to_adjust;
         element_to_adjust = GetParentForDirectionality(
             *element_to_adjust, needs_slot_assignment_recalc)) {
      if (ElementAffectsDirectionality(element_to_adjust)) {
        directionality_set.insert(element_to_adjust);
        continue;
      }
    }
  }

  for (auto& element : directionality_set) {
    if (element->CalculateAndAdjustAutoDirectionality(element) &&
        RuntimeEnabledFeatures::CSSPseudoDirEnabled()) {
      element->SetNeedsStyleRecalc(kLocalStyleChange,
                                   StyleChangeReasonForTracing::Create(
                                       style_change_reason::kPseudoClass));
    }
  }
}

Node::InsertionNotificationRequest HTMLElement::InsertedInto(
    ContainerNode& insertion_point) {
  // Process the superclass first to ensure that `InActiveDocument()` is
  // updated.
  Element::InsertedInto(insertion_point);
  HideNonce();

  if (IsFormAssociatedCustomElement())
    EnsureElementInternals().InsertedInto(insertion_point);

  if (GetPopoverData() && GetPopoverData()->hadDefaultOpenWhenParsed()) {
    // If a Popover element has the `defaultopen` attribute upon page
    // load, and it is the *first* such popover, show it.
    DCHECK(RuntimeEnabledFeatures::HTMLPopoverAttributeEnabled(
        GetDocument().GetExecutionContext()));
    DCHECK(isConnected());
    GetPopoverData()->setHadDefaultOpenWhenParsed(false);
    auto maybe_show_popover = [](HTMLElement* popover) {
      // The `defaultopen` attribute can only be used on popover=manual and
      // popover=auto popovers.
      if (popover && popover->isConnected() &&
          (popover->PopoverType() == PopoverValueType::kManual ||
           (popover->PopoverType() == PopoverValueType::kAuto &&
            !popover->GetDocument().PopoverAutoShowing()))) {
        popover->showPopover(ASSERT_NO_EXCEPTION);
      }
    };
    GetDocument()
        .GetTaskRunner(TaskType::kDOMManipulation)
        ->PostTask(FROM_HERE,
                   WTF::BindOnce(maybe_show_popover, WrapWeakPersistent(this)));
  }

  return kInsertionDone;
}

void HTMLElement::RemovedFrom(ContainerNode& insertion_point) {
  // If a popover is removed from the document, make sure it gets
  // removed from the popover element stack and the top layer.
  bool was_in_document = insertion_point.isConnected();
  if (was_in_document && HasPopoverAttribute()) {
    // We can't run focus event handlers while removing elements.
    HidePopoverInternal(HidePopoverFocusBehavior::kNone,
                        HidePopoverForcingLevel::kHideImmediately);
  }

  Element::RemovedFrom(insertion_point);
  if (IsFormAssociatedCustomElement())
    EnsureElementInternals().RemovedFrom(insertion_point);
}

void HTMLElement::DidMoveToNewDocument(Document& old_document) {
  if (IsFormAssociatedCustomElement())
    EnsureElementInternals().DidMoveToNewDocument(old_document);
  Element::DidMoveToNewDocument(old_document);
}

void HTMLElement::AddHTMLLengthToStyle(MutableCSSPropertyValueSet* style,
                                       CSSPropertyID property_id,
                                       const String& value,
                                       AllowPercentage allow_percentage,
                                       AllowZero allow_zero) {
  HTMLDimension dimension;
  if (!ParseDimensionValue(value, dimension))
    return;
  if (property_id == CSSPropertyID::kWidth &&
      (dimension.IsPercentage() || dimension.IsRelative())) {
    UseCounter::Count(GetDocument(), WebFeature::kHTMLElementDeprecatedWidth);
  }
  if (dimension.IsRelative())
    return;
  if (dimension.IsPercentage() &&
      allow_percentage == kDontAllowPercentageValues)
    return;
  if (dimension.Value() == 0 && allow_zero == kDontAllowZeroValues)
    return;
  CSSPrimitiveValue::UnitType unit =
      dimension.IsPercentage() ? CSSPrimitiveValue::UnitType::kPercentage
                               : CSSPrimitiveValue::UnitType::kPixels;
  AddPropertyToPresentationAttributeStyle(style, property_id, dimension.Value(),
                                          unit);
}

static Color ParseColorStringWithCrazyLegacyRules(const String& color_string) {
  // Per spec, only look at the first 128 digits of the string.
  const size_t kMaxColorLength = 128;
  // We'll pad the buffer with two extra 0s later, so reserve two more than the
  // max.
  Vector<char, kMaxColorLength + 2> digit_buffer;

  wtf_size_t i = 0;
  // Skip a leading #.
  if (color_string[0] == '#')
    i = 1;

  // Grab the first 128 characters, replacing non-hex characters with 0.
  // Non-BMP characters are replaced with "00" due to them appearing as two
  // "characters" in the String.
  for (; i < color_string.length() && digit_buffer.size() < kMaxColorLength;
       i++) {
    if (!IsASCIIHexDigit(color_string[i]))
      digit_buffer.push_back('0');
    else
      digit_buffer.push_back(color_string[i]);
  }

  if (!digit_buffer.size())
    return Color::kBlack;

  // Pad the buffer out to at least the next multiple of three in size.
  digit_buffer.push_back('0');
  digit_buffer.push_back('0');

  if (digit_buffer.size() < 6) {
    return Color::FromRGB(ToASCIIHexValue(digit_buffer[0]),
                          ToASCIIHexValue(digit_buffer[1]),
                          ToASCIIHexValue(digit_buffer[2]));
  }

  // Split the digits into three components, then search the last 8 digits of
  // each component.
  DCHECK_GE(digit_buffer.size(), 6u);
  wtf_size_t component_length = digit_buffer.size() / 3;
  wtf_size_t component_search_window_length =
      std::min<wtf_size_t>(component_length, 8);
  wtf_size_t red_index = component_length - component_search_window_length;
  wtf_size_t green_index =
      component_length * 2 - component_search_window_length;
  wtf_size_t blue_index = component_length * 3 - component_search_window_length;
  // Skip digits until one of them is non-zero, or we've only got two digits
  // left in the component.
  while (digit_buffer[red_index] == '0' && digit_buffer[green_index] == '0' &&
         digit_buffer[blue_index] == '0' &&
         (component_length - red_index) > 2) {
    red_index++;
    green_index++;
    blue_index++;
  }
  DCHECK_LT(red_index + 1, component_length);
  DCHECK_GE(green_index, component_length);
  DCHECK_LT(green_index + 1, component_length * 2);
  DCHECK_GE(blue_index, component_length * 2);
  SECURITY_DCHECK(blue_index + 1 < digit_buffer.size());

  int red_value =
      ToASCIIHexValue(digit_buffer[red_index], digit_buffer[red_index + 1]);
  int green_value =
      ToASCIIHexValue(digit_buffer[green_index], digit_buffer[green_index + 1]);
  int blue_value =
      ToASCIIHexValue(digit_buffer[blue_index], digit_buffer[blue_index + 1]);
  return Color::FromRGB(red_value, green_value, blue_value);
}

// Color parsing that matches HTML's "rules for parsing a legacy color value"
bool HTMLElement::ParseColorWithLegacyRules(const String& attribute_value,
                                            Color& parsed_color) {
  // An empty string doesn't apply a color. (One containing only whitespace
  // does, which is why this check occurs before stripping.)
  if (attribute_value.empty())
    return false;

  String color_string = attribute_value.StripWhiteSpace();

  // "transparent" doesn't apply a color either.
  if (EqualIgnoringASCIICase(color_string, "transparent"))
    return false;

  // If the string is a 3/6-digit hex color or a named CSS color, use that.
  // Apply legacy rules otherwise. Note color.setFromString() accepts 4/8-digit
  // hex color, so restrict its use with length checks here to support legacy
  // HTML attributes.

  bool success = false;
  if ((color_string.length() == 4 || color_string.length() == 7) &&
      color_string[0] == '#')
    success = parsed_color.SetFromString(color_string);
  if (!success)
    success = parsed_color.SetNamedColor(color_string);
  if (!success) {
    parsed_color = ParseColorStringWithCrazyLegacyRules(color_string);
    success = true;
  }

  return success;
}

void HTMLElement::AddHTMLColorToStyle(MutableCSSPropertyValueSet* style,
                                      CSSPropertyID property_id,
                                      const String& attribute_value) {
  Color parsed_color;
  if (!ParseColorWithLegacyRules(attribute_value, parsed_color))
    return;

  style->SetProperty(property_id, *cssvalue::CSSColor::Create(parsed_color));
}

LabelsNodeList* HTMLElement::labels() {
  if (!IsLabelable())
    return nullptr;
  return EnsureCachedCollection<LabelsNodeList>(kLabelsNodeListType);
}

bool HTMLElement::IsInteractiveContent() const {
  return false;
}

void HTMLElement::DefaultEventHandler(Event& event) {
  auto* keyboard_event = DynamicTo<KeyboardEvent>(event);
  if (event.type() == event_type_names::kKeypress && keyboard_event) {
    HandleKeypressEvent(*keyboard_event);
    if (event.DefaultHandled())
      return;
  }

  Element::DefaultEventHandler(event);
}

bool HTMLElement::HandleKeyboardActivation(Event& event) {
  auto* keyboard_event = DynamicTo<KeyboardEvent>(event);
  if (keyboard_event) {
    if (event.type() == event_type_names::kKeydown &&
        keyboard_event->key() == " ") {
      SetActive(true);
      // No setDefaultHandled() - IE dispatches a keypress in this case.
      return true;
    }
    if (event.type() == event_type_names::kKeypress) {
      switch (keyboard_event->charCode()) {
        case '\r':
          DispatchSimulatedClick(&event);
          event.SetDefaultHandled();
          return true;
        case ' ':
          // Prevent scrolling down the page.
          event.SetDefaultHandled();
          return true;
      }
    }
    if (event.type() == event_type_names::kKeyup &&
        keyboard_event->key() == " ") {
      if (IsActive())
        DispatchSimulatedClick(&event);
      event.SetDefaultHandled();
      return true;
    }
  }
  return false;
}

bool HTMLElement::MatchesReadOnlyPseudoClass() const {
  return !MatchesReadWritePseudoClass();
}

// https://html.spec.whatwg.org/multipage/semantics-other.html#selector-read-write
// The :read-write pseudo-class must match ... elements that are editing hosts
// or editable and are neither input elements nor textarea elements
bool HTMLElement::MatchesReadWritePseudoClass() const {
  return IsEditableOrEditingHost(*this);
}

void HTMLElement::HandleKeypressEvent(KeyboardEvent& event) {
  if (!IsSpatialNavigationEnabled(GetDocument().GetFrame()) || !SupportsFocus())
    return;
  if (RuntimeEnabledFeatures::FocuslessSpatialNavigationEnabled())
    return;
  GetDocument().UpdateStyleAndLayoutTree();
  // if the element is a text form control (like <input type=text> or
  // <textarea>) or has contentEditable attribute on, we should enter a space or
  // newline even in spatial navigation mode instead of handling it as a "click"
  // action.
  if (IsTextControl() || IsEditable(*this))
    return;
  int char_code = event.charCode();
  if (char_code == '\r' || char_code == ' ') {
    DispatchSimulatedClick(&event);
    event.SetDefaultHandled();
  }
}

int HTMLElement::AdjustedOffsetForZoom(LayoutUnit offset) {
  const auto* layout_object = GetLayoutObject();
  DCHECK(layout_object);
  return AdjustForAbsoluteZoom::AdjustLayoutUnit(offset,
                                                 layout_object->StyleRef())
      .Round();
}

int HTMLElement::OffsetTopOrLeft(bool top) {
  GetDocument().EnsurePaintLocationDataValidForNode(
      this, DocumentUpdateReason::kJavaScript);
  const auto* layout_object = GetLayoutBoxModelObject();
  if (!layout_object)
    return 0;

  HashSet<Member<TreeScope>> ancestor_tree_scopes = GetAncestorTreeScopes();
  LayoutUnit offset;
  Element* offset_parent = this;
  bool new_spec_behavior =
      RuntimeEnabledFeatures::OffsetParentNewSpecBehaviorEnabled();
  // This loop adds up all of the offsetTop/offsetLeft values for this and
  // parent shadow-hidden offsetParents up the flat tree. If
  // |ancestor_tree_scopes| doesn't contain the next |offset_parent|'s
  // TreeScope, then we know that |offset_parent| is shadow-hidden from |this|.
  do {
    // offset_parent->OffsetParent() may update style and layout:
    Element* next_offset_parent = offset_parent->OffsetParent();
    if (const auto* offset_parent_layout_object =
            offset_parent->GetLayoutBoxModelObject()) {
      if (top) {
        offset += offset_parent_layout_object->OffsetTop(next_offset_parent);
      } else {
        offset += offset_parent_layout_object->OffsetLeft(next_offset_parent);
      }
    }
    offset_parent = next_offset_parent;
  } while (new_spec_behavior && offset_parent &&
           !ancestor_tree_scopes.Contains(&offset_parent->GetTreeScope()));

  return AdjustedOffsetForZoom(offset);
}

int HTMLElement::offsetLeftForBinding() {
  return OffsetTopOrLeft(/*top=*/false);
}

int HTMLElement::offsetTopForBinding() {
  return OffsetTopOrLeft(/*top=*/true);
}

int HTMLElement::offsetWidthForBinding() {
  GetDocument().EnsurePaintLocationDataValidForNode(
      this, DocumentUpdateReason::kJavaScript, CSSPropertyID::kWidth);
  int result = 0;
  if (const auto* layout_object = GetLayoutBoxModelObject()) {
    result = AdjustedOffsetForZoom(layout_object->OffsetWidth());
    RecordScrollbarSizeForStudy(result, /* is_width= */ true,
                                /* is_offset= */ true);
  }
  return result;
}

DISABLE_CFI_PERF
int HTMLElement::offsetHeightForBinding() {
  GetDocument().EnsurePaintLocationDataValidForNode(
      this, DocumentUpdateReason::kJavaScript, CSSPropertyID::kHeight);
  int result = 0;
  if (const auto* layout_object = GetLayoutBoxModelObject()) {
    result = AdjustedOffsetForZoom(layout_object->OffsetHeight());
    RecordScrollbarSizeForStudy(result, /* is_width= */ false,
                                /* is_offset= */ true);
  }
  return result;
}

Element* HTMLElement::unclosedOffsetParent() {
  GetDocument().UpdateStyleAndLayoutForNode(this,
                                            DocumentUpdateReason::kJavaScript);

  LayoutObject* layout_object = GetLayoutObject();
  if (!layout_object)
    return nullptr;

  return layout_object->OffsetParent(this);
}

void HTMLElement::UpdateDescendantHasDirAutoAttribute(bool has_dir_auto) {
  Node* node = FlatTreeTraversal::FirstChild(*this);
  while (node) {
    if (auto* element = DynamicTo<Element>(node)) {
      AtomicString dir_attribute_value =
          element->FastGetAttribute(html_names::kDirAttr);
      if (IsValidDirAttribute(dir_attribute_value)) {
        node = FlatTreeTraversal::NextSkippingChildren(*node, this);
        continue;
      }

      if (auto* slot = ToHTMLSlotElementIfSupportsAssignmentOrNull(node)) {
        ShadowRoot* root = slot->ContainingShadowRoot();
        // Defer to adjust the directionality to avoid recalcuating slot
        // assignment in FlatTreeTraversal when updating slot.
        // Slot and its children will be updated after recalculating children.
        if (root->NeedsSlotAssignmentRecalc()) {
          root->SetNeedsDirAutoAttributeUpdate(true);
          node = FlatTreeTraversal::NextSkippingChildren(*node, this);
          continue;
        }
      }

      if (!has_dir_auto) {
        if (!element->SelfOrAncestorHasDirAutoAttribute()) {
          node = FlatTreeTraversal::NextSkippingChildren(*node, this);
          continue;
        }
        element->ClearSelfOrAncestorHasDirAutoAttribute();
      } else {
        if (element->SelfOrAncestorHasDirAutoAttribute()) {
          node = FlatTreeTraversal::NextSkippingChildren(*node, this);
          continue;
        }
        element->SetSelfOrAncestorHasDirAutoAttribute();
      }
    }
    node = FlatTreeTraversal::Next(*node, this);
  }
}

void HTMLElement::UpdateDirectionalityAndDescendant(TextDirection direction) {
  SetCachedDirectionality(direction);
  UpdateDescendantDirectionality(direction);
}

void HTMLElement::UpdateDescendantDirectionality(TextDirection direction) {
  Node* node = FlatTreeTraversal::FirstChild(*this);
  while (node) {
    if (IsA<HTMLElement>(node)) {
      if (ElementAffectsDirectionality(node) ||
          node->CachedDirectionality() == direction) {
        node = FlatTreeTraversal::NextSkippingChildren(*node, this);
        continue;
      }

      node->SetCachedDirectionality(direction);
      if (auto* slot = ToHTMLSlotElementIfSupportsAssignmentOrNull(node)) {
        ShadowRoot* root = slot->ContainingShadowRoot();
        // Defer to update the directionality of slot's descendant to avoid
        // recalcuating slot assignment in FlatTreeTraversal when updating slot.
        if (root->NeedsSlotAssignmentRecalc()) {
          node = FlatTreeTraversal::NextSkippingChildren(*node, this);
          continue;
        }
      }
    }
    node = FlatTreeTraversal::Next(*node, this);
  }
}

void HTMLElement::OnDirAttrChanged(const AttributeModificationParams& params) {
  // If an ancestor has dir=auto, and this node has the first character,
  // changes to dir attribute may affect the ancestor.
  if (!IsValidDirAttribute(params.old_value) &&
      !IsValidDirAttribute(params.new_value))
    return;

  GetDocument().SetDirAttributeDirty();

  bool is_old_auto = SelfOrAncestorHasDirAutoAttribute();
  bool is_new_auto = HasDirectionAuto();
  bool needs_slot_assignment_recalc = false;
  auto* parent =
      GetParentForDirectionality(*this, needs_slot_assignment_recalc);
  if (!is_old_auto || !is_new_auto) {
    if (parent && parent->SelfOrAncestorHasDirAutoAttribute()) {
      parent->AdjustDirectionalityIfNeededAfterChildAttributeChanged(this);
    }
  }

  if (is_old_auto && !is_new_auto) {
    ClearSelfOrAncestorHasDirAutoAttribute();
    UpdateDescendantHasDirAutoAttribute(false /* has_dir_auto */);
  } else if (!is_old_auto && is_new_auto) {
    SetSelfOrAncestorHasDirAutoAttribute();
    UpdateDescendantHasDirAutoAttribute(true /* has_dir_auto */);
  }

  if (is_new_auto) {
    CalculateAndAdjustAutoDirectionality(this);
  } else {
    absl::optional<TextDirection> text_direction;
    if (EqualIgnoringASCIICase(params.new_value, "ltr")) {
      text_direction = TextDirection::kLtr;
    } else if (EqualIgnoringASCIICase(params.new_value, "rtl")) {
      text_direction = TextDirection::kRtl;
    }

    if (!text_direction.has_value()) {
      if (parent) {
        text_direction = parent->CachedDirectionality();
      } else {
        text_direction = TextDirection::kLtr;
      }
    }

    if (needs_slot_assignment_recalc) {
      SetNeedsInheritDirectionalityFromParent();
    } else {
      UpdateDirectionalityAndDescendant(*text_direction);
    }
  }

  if (RuntimeEnabledFeatures::CSSPseudoDirEnabled()) {
    SetNeedsStyleRecalc(
        kSubtreeStyleChange,
        StyleChangeReasonForTracing::Create(style_change_reason::kPseudoClass));
    PseudoStateChanged(CSSSelector::kPseudoDir);
  }
}

void HTMLElement::ReparseAttribute(const AttributeModificationParams& params) {
  if (params.name == html_names::kPopoverAttr) {
    UpdatePopoverAttribute(params.new_value);
  }
  Element::ParseAttribute(params);
}

void HTMLElement::OnFormAttrChanged(const AttributeModificationParams& params) {
  if (IsFormAssociatedCustomElement())
    EnsureElementInternals().FormAttributeChanged();
}

void HTMLElement::OnLangAttrChanged(const AttributeModificationParams& params) {
  PseudoStateChanged(CSSSelector::kPseudoLang);
}

void HTMLElement::OnNonceAttrChanged(
    const AttributeModificationParams& params) {
  if (params.new_value != g_empty_atom)
    setNonce(params.new_value);
}

ElementInternals* HTMLElement::attachInternals(
    ExceptionState& exception_state) {
  // 1. If this's is value is not null, then throw a "NotSupportedError"
  // DOMException.
  if (IsValue()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "Unable to attach ElementInternals to a customized built-in element.");
    return nullptr;
  }

  // 2. Let definition be the result of looking up a custom element definition
  // given this's node document, its namespace, its local name, and null as the
  // is value.
  CustomElementRegistry* registry = CustomElement::Registry(*this);
  auto* definition =
      registry ? registry->DefinitionForName(localName()) : nullptr;

  // 3. If definition is null, then throw an "NotSupportedError" DOMException.
  if (!definition) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "Unable to attach ElementInternals to non-custom elements.");
    return nullptr;
  }

  // 4. If definition's disable internals is true, then throw a
  // "NotSupportedError" DOMException.
  if (definition->DisableInternals()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "ElementInternals is disabled by disabledFeature static field.");
    return nullptr;
  }

  // 5. If this's attached internals is true, then throw an "NotSupportedError"
  // DOMException.
  if (DidAttachInternals()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "ElementInternals for the specified element was already attached.");
    return nullptr;
  }

  // 6. If this's custom element state is not "precustomized" or "custom", then
  // throw a "NotSupportedError" DOMException.
  if (GetCustomElementState() != CustomElementState::kCustom &&
      GetCustomElementState() != CustomElementState::kPreCustomized) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "The attachInternals() function cannot be called prior to the "
        "execution of the custom element constructor.");
    return nullptr;
  }

  // 7. Set this's attached internals to true.
  SetDidAttachInternals();
  // 8. Return a new ElementInternals instance whose target element is this.
  UseCounter::Count(GetDocument(), WebFeature::kElementAttachInternals);
  return &EnsureElementInternals();
}

bool HTMLElement::IsFormAssociatedCustomElement() const {
  return GetCustomElementState() == CustomElementState::kCustom &&
         GetCustomElementDefinition()->IsFormAssociated();
}

bool HTMLElement::SupportsFocus() const {
  return Element::SupportsFocus() && !IsDisabledFormControl();
}

bool HTMLElement::IsDisabledFormControl() const {
  if (!IsFormAssociatedCustomElement())
    return false;
  return const_cast<HTMLElement*>(this)
      ->EnsureElementInternals()
      .IsActuallyDisabled();
}

bool HTMLElement::MatchesEnabledPseudoClass() const {
  return IsFormAssociatedCustomElement() && !const_cast<HTMLElement*>(this)
                                                 ->EnsureElementInternals()
                                                 .IsActuallyDisabled();
}

bool HTMLElement::MatchesValidityPseudoClasses() const {
  return IsFormAssociatedCustomElement();
}

bool HTMLElement::willValidate() const {
  return IsFormAssociatedCustomElement() && const_cast<HTMLElement*>(this)
                                                ->EnsureElementInternals()
                                                .WillValidate();
}

bool HTMLElement::IsValidElement() {
  return IsFormAssociatedCustomElement() &&
         EnsureElementInternals().IsValidElement();
}

bool HTMLElement::IsLabelable() const {
  return IsFormAssociatedCustomElement();
}

void HTMLElement::FinishParsingChildren() {
  Element::FinishParsingChildren();
  if (IsFormAssociatedCustomElement())
    EnsureElementInternals().TakeStateAndRestore();
}

void HTMLElement::BeginParsingChildren() {
  Element::BeginParsingChildren();

  if (GetDocument().IsDirAttributeDirty() && !HasDirectionAuto() &&
      !ElementAffectsDirectionality(this)) {
    bool needs_slot_assignment_recalc = false;
    auto* parent =
        GetParentForDirectionality(*this, needs_slot_assignment_recalc);
    if (needs_slot_assignment_recalc)
      SetNeedsInheritDirectionalityFromParent();
    else if (parent)
      SetCachedDirectionality(parent->CachedDirectionality());
  }
}

}  // namespace blink

#ifndef NDEBUG

// For use in the debugger
void dumpInnerHTML(blink::HTMLElement*);

void dumpInnerHTML(blink::HTMLElement* element) {
  printf("%s\n", element->innerHTML().Ascii().c_str());
}

#endif
