// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/accessibility/render_accessibility_impl.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/containers/queue.h"
#include "base/debug/crash_logging.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/timer/elapsed_timer.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "content/public/common/content_features.h"
#include "content/public/renderer/render_thread.h"
#include "content/renderer/accessibility/ax_action_target_factory.h"
#include "content/renderer/accessibility/ax_image_annotator.h"
#include "content/renderer/accessibility/ax_tree_snapshotter_impl.h"
#include "content/renderer/accessibility/blink_ax_action_target.h"
#include "content/renderer/accessibility/render_accessibility_manager.h"
#include "content/renderer/render_frame_impl.h"
#include "services/image_annotation/public/mojom/image_annotation.mojom.h"
#include "services/metrics/public/cpp/mojo_ukm_recorder.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/public/web/web_disallow_transition_scope.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_input_element.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_page_popup.h"
#include "third_party/blink/public/web/web_settings.h"
#include "third_party/blink/public/web/web_view.h"
#include "ui/accessibility/accessibility_features.h"
#include "ui/accessibility/accessibility_switches.h"
#include "ui/accessibility/ax_enum_util.h"
#include "ui/accessibility/ax_event_intent.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_role_properties.h"
#include "ui/accessibility/ax_tree_id.h"
#include "ui/gfx/geometry/vector2d_conversions.h"

using blink::WebAXContext;
using blink::WebAXObject;
using blink::WebDocument;
using blink::WebElement;
using blink::WebLocalFrame;
using blink::WebNode;
using blink::WebSettings;
using blink::WebView;

namespace {

// The minimum amount of time that should be spent in serializing code in order
// to report the elapsed time as a URL-keyed metric.
constexpr base::TimeDelta kMinSerializationTimeToSend = base::Milliseconds(100);

// When URL-keyed metrics for the amount of time spent in serializing code
// are sent, the minimum amount of time to wait, in seconds, before
// sending metrics. Metrics may also be sent once per page transition.
constexpr base::TimeDelta kMinUKMDelay = base::Seconds(300);

void SetAccessibilityCrashKey(ui::AXMode mode) {
  // Add a crash key with the ax_mode, to enable searching for top crashes that
  // occur when accessibility is turned on. This adds it for each renderer,
  // process, and elsewhere the same key is added for the browser process.
  // Note: in theory multiple renderers in the same process might not have the
  // same mode. As an example, kLabelImages could be enabled for just one
  // renderer. The presence if a mode flag means in a crash report means at
  // least one renderer in the same process had that flag.
  // Examples of when multiple renderers could share the same process:
  // 1) Android, 2) When many tabs are open.
  static auto* const ax_mode_crash_key = base::debug::AllocateCrashKeyString(
      "ax_mode", base::debug::CrashKeySize::Size64);
  base::debug::SetCrashKeyString(ax_mode_crash_key, mode.ToString());
}

}  // namespace

namespace content {

// Create this on the stack to freeze BlinkAXTreeSource and automatically
// un-freeze it when it goes out of scope.
class ScopedFreezeAXTreeSource {
 public:
  explicit ScopedFreezeAXTreeSource(blink::WebAXContext* context)
      : context_(context) {
    if (context_)
      context_->Freeze();
  }

  ScopedFreezeAXTreeSource(const ScopedFreezeAXTreeSource&) = delete;
  ScopedFreezeAXTreeSource& operator=(const ScopedFreezeAXTreeSource&) = delete;

  ~ScopedFreezeAXTreeSource() {
    if (context_)
      context_->Thaw();
  }

 private:
  blink::WebAXContext* context_;
};

RenderAccessibilityImpl::RenderAccessibilityImpl(
    RenderAccessibilityManager* const render_accessibility_manager,
    RenderFrameImpl* const render_frame,
    ui::AXMode mode)
    : RenderFrameObserver(render_frame),
      render_accessibility_manager_(render_accessibility_manager),
      render_frame_(render_frame),
      plugin_tree_source_(nullptr),
      event_schedule_status_(EventScheduleStatus::kNotWaiting),
      reset_token_(0),
      ukm_timer_(std::make_unique<base::ElapsedTimer>()),
      last_ukm_source_id_(ukm::kInvalidSourceId),
      accessibility_mode_(mode) {
  mojo::PendingRemote<ukm::mojom::UkmRecorderInterface> recorder;
  content::RenderThread::Get()->BindHostReceiver(
      recorder.InitWithNewPipeAndPassReceiver());
  ukm_recorder_ = std::make_unique<ukm::MojoUkmRecorder>(std::move(recorder));
  WebView* web_view = render_frame_->GetWebView();
  WebSettings* settings = web_view->GetSettings();

  SetAccessibilityCrashKey(mode);
#if BUILDFLAG(IS_ANDROID)
  // Password values are only passed through on Android.
  settings->SetAccessibilityPasswordValuesEnabled(true);
#endif

#if !BUILDFLAG(IS_ANDROID)
  // Inline text boxes can be enabled globally on all except Android.
  // On Android they can be requested for just a specific node.
  if (mode.has_mode(ui::AXMode::kInlineTextBoxes))
    settings->SetInlineTextBoxAccessibilityEnabled(true);
#endif

#if BUILDFLAG(IS_MAC)
  // aria-modal currently prunes the accessibility tree on Mac only.
  settings->SetAriaModalPrunesAXTree(true);
#endif

#if BUILDFLAG(IS_CHROMEOS)
  // Do not ignore SVG grouping (<g>) elements on ChromeOS, which is needed so
  // Select-to-Speak can read SVG text nodes in natural reading order.
  settings->SetAccessibilityIncludeSvgGElement(true);
#endif

  event_schedule_mode_ = EventScheduleMode::kDeferEvents;

  // Optionally disable AXMenuList, which makes the internal pop-up menu
  // UI for a select element directly accessible. Disable by default on
  // Chrome OS, but some tests may override.
  bool disable_ax_menu_list = false;
#if BUILDFLAG(IS_CHROMEOS)
  disable_ax_menu_list = true;
#endif
  auto* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(::switches::kDisableAXMenuList)) {
    disable_ax_menu_list = command_line->GetSwitchValueASCII(
                               ::switches::kDisableAXMenuList) != "false";
  }
  if (disable_ax_menu_list)
    settings->SetUseAXMenuList(false);

  const WebDocument& document = GetMainDocument();
  if (!document.IsNull()) {
    ax_context_ = std::make_unique<WebAXContext>(document, mode);
    StartOrStopLabelingImages(ui::AXMode(), mode);

    // It's possible that the webview has already loaded a webpage without
    // accessibility being enabled. Initialize the browser's cached
    // accessibility tree by firing a layout complete for the document.
    // Ensure that this occurs after initial layout is actually complete.
    ScheduleSendPendingAccessibilityEvents();
  }

  image_annotation_debugging_ =
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          ::switches::kEnableExperimentalAccessibilityLabelsDebugging);
}

RenderAccessibilityImpl::~RenderAccessibilityImpl() = default;

void RenderAccessibilityImpl::DidCreateNewDocument() {
  const WebDocument& document = GetMainDocument();
  if (!document.IsNull())
    ax_context_ = std::make_unique<WebAXContext>(document, accessibility_mode_);
}

void RenderAccessibilityImpl::DidCommitProvisionalLoad(
    ui::PageTransition transition) {
  has_injected_stylesheet_ = false;

  // If we have events scheduled, but not sent, cancel them
  CancelScheduledEvents();
  // Defer events during initial page load.
  event_schedule_mode_ = EventScheduleMode::kDeferEvents;

  MaybeSendUKM();
  slowest_serialization_time_ = base::TimeDelta();
  ukm_timer_ = std::make_unique<base::ElapsedTimer>();

  // Remove the image annotator if the page is loading and it was added for
  // the one-shot image annotation (i.e. AXMode for image annotation is not
  // set).
  if (!ax_image_annotator_ ||
      accessibility_mode_.has_mode(ui::AXMode::kLabelImages)) {
    return;
  }
  ax_image_annotator_->Destroy();
  ax_image_annotator_.reset();
  page_language_.clear();
}

void RenderAccessibilityImpl::AccessibilityModeChanged(const ui::AXMode& mode) {
  ui::AXMode old_mode = accessibility_mode_;
  if (old_mode == mode)
    return;
  accessibility_mode_ = mode;

  // TODO(aleventhal): DCHECK(!mode.is_mode_off()), because this object
  // should be deleted before that mode is set.
  if (mode.is_mode_off()) {
    ax_context_ = nullptr;
    return;
  } else if (ax_context_) {
    ax_context_->SetAXMode(mode);
  } else {
    return;
  }

  DCHECK(ax_context_);
  DCHECK_EQ(accessibility_mode_, ax_context_->GetAXMode());

  SetAccessibilityCrashKey(mode);

#if !BUILDFLAG(IS_ANDROID)
  // Inline text boxes can be enabled globally on all except Android.
  // On Android they can be requested for just a specific node.
  WebView* web_view = render_frame_->GetWebView();
  if (web_view) {
    WebSettings* settings = web_view->GetSettings();
    if (settings) {
      if (mode.has_mode(ui::AXMode::kInlineTextBoxes)) {
        settings->SetInlineTextBoxAccessibilityEnabled(true);
        ax_context_->UpdateAXForAllDocuments();
        ComputeRoot().LoadInlineTextBoxes();
      } else {
        settings->SetInlineTextBoxAccessibilityEnabled(false);
      }
    }
  }
#endif  // !BUILDFLAG(IS_ANDROID)

  ax_context_->ResetSerializer();
  const WebDocument& document = GetMainDocument();
  if (!document.IsNull()) {
    StartOrStopLabelingImages(old_mode, mode);

    needs_initial_ax_tree_root_ = true;
    event_schedule_mode_ = EventScheduleMode::kProcessEventsImmediately;
    ScheduleSendPendingAccessibilityEvents();
  }
}

// This function expects the |point| passed by parameter to be relative to the
// page viewport, always. This means that when the position is within a popup,
// the |point| should still be relative to the web page's viewport.
void RenderAccessibilityImpl::HitTest(
    const gfx::Point& point,
    ax::mojom::Event event_to_fire,
    int request_id,
    blink::mojom::RenderAccessibility::HitTestCallback callback) {
  const WebDocument& document = GetMainDocument();
  DCHECK(!document.IsNull());
  ax_context_->UpdateAXForAllDocuments();

  WebAXObject ax_object;
  // 1. Now that layout has been updated for the entire document, try to run
  // the hit test operation on the popup root element, if there's a popup
  // opened. This is needed to allow hit testing within web content popups.
  absl::optional<gfx::RectF> popup_bounds = GetPopupBounds();
  if (popup_bounds.has_value()) {
    auto popup_root_obj = WebAXObject::FromWebDocument(GetPopupDocument());
    // WebAXObject::HitTest expects the point passed by parameter to be
    // relative to the instance we call it from.
    ax_object = popup_root_obj.HitTest(
        point - ToRoundedVector2d(popup_bounds->OffsetFromOrigin()));
  }

  // 2. If running the hit test operation on the popup didn't returned any
  // result (or if there was no popup), run the hit test operation from the
  // main element.
  if (ax_object.IsNull()) {
    auto root_obj = WebAXObject::FromWebDocument(document);
    ax_object = root_obj.HitTest(point);
  }

  // Return if no attached accessibility object was found for the main document.
  if (ax_object.IsDetached()) {
    std::move(callback).Run(/*hit_test_response=*/nullptr);
    return;
  }

  // If the result was in the same frame, return the result.
  ui::AXNodeData data;
  ScopedFreezeAXTreeSource freeze(ax_context_.get());
  ax_object.Serialize(&data, ax_context_->GetAXMode());
  if (!data.HasStringAttribute(ax::mojom::StringAttribute::kChildTreeId)) {
    // Optionally fire an event, if requested to. This is a good fit for
    // features like touch exploration on Android, Chrome OS, and
    // possibly other platforms - if the user explore a particular point,
    // we fire a hover event on the nearest object under the point.
    //
    // Avoid using this mechanism to fire a particular sentinel event
    // and then listen for that event to associate it with the hit test
    // request. Instead, the mojo reply should be used directly.
    if (event_to_fire != ax::mojom::Event::kNone) {
      const std::vector<ui::AXEventIntent> intents;
      HandleAXEvent(ui::AXEvent(
          ax_object.AxID(), event_to_fire, ax::mojom::EventFrom::kAction,
          ax::mojom::Action::kHitTest, intents, request_id));
    }

    // Reply with the result.
    const auto& frame_token = render_frame_->GetWebFrame()->GetFrameToken();
    std::move(callback).Run(blink::mojom::HitTestResponse::New(
        frame_token, point, ax_object.AxID()));
    return;
  }

  // The result was in a child frame. Reply so that the
  // client can do a hit test on the child frame recursively.
  // If it's a remote frame, transform the point into the child frame's
  // coordinate system.
  gfx::Point transformed_point = point;
  blink::WebFrame* child_frame =
      blink::WebFrame::FromFrameOwnerElement(ax_object.GetNode());
  DCHECK(child_frame);

  if (child_frame->IsWebRemoteFrame()) {
    // Remote frames don't have access to the information from the visual
    // viewport regarding the visual viewport offset, so we adjust the
    // coordinates before sending them to the remote renderer.
    gfx::Rect rect = ax_object.GetBoundsInFrameCoordinates();
    // The following transformation of the input point is naive, but works
    // fairly well. It will fail with CSS transforms that rotate or shear.
    // https://crbug.com/981959.
    WebView* web_view = render_frame_->GetWebView();
    gfx::PointF viewport_offset = web_view->VisualViewportOffset();
    transformed_point +=
        gfx::Vector2d(viewport_offset.x(), viewport_offset.y()) -
        rect.OffsetFromOrigin();
  }

  std::move(callback).Run(blink::mojom::HitTestResponse::New(
      child_frame->GetFrameToken(), transformed_point, ax_object.AxID()));
}

void RenderAccessibilityImpl::PerformAction(const ui::AXActionData& data) {
  WebDocument document = GetMainDocument();
  if (document.IsNull())
    return;

  ax_context_->UpdateAXForAllDocuments();

  // If an action was requested, we no longer want to defer events.
  event_schedule_mode_ = EventScheduleMode::kProcessEventsImmediately;

  std::unique_ptr<ui::AXActionTarget> target =
      AXActionTargetFactory::CreateFromNodeId(document, plugin_tree_source_,
                                              data.target_node_id);
  std::unique_ptr<ui::AXActionTarget> anchor =
      AXActionTargetFactory::CreateFromNodeId(document, plugin_tree_source_,
                                              data.anchor_node_id);
  std::unique_ptr<ui::AXActionTarget> focus =
      AXActionTargetFactory::CreateFromNodeId(document, plugin_tree_source_,
                                              data.focus_node_id);

  // Important: keep this reconciled with AXObject::PerformAction().
  // Actions shouldn't be handled in both places.
  switch (data.action) {
    case ax::mojom::Action::kGetImageData:
      OnGetImageData(target.get(), data.target_rect.size());
      break;
    case ax::mojom::Action::kLoadInlineTextBoxes:
      OnLoadInlineTextBoxes(target.get());
      break;
    case ax::mojom::Action::kSetSelection:
      anchor->SetSelection(anchor.get(), data.anchor_offset, focus.get(),
                           data.focus_offset);
      break;
    case ax::mojom::Action::kScrollToMakeVisible:
      target->ScrollToMakeVisibleWithSubFocus(
          data.target_rect, data.horizontal_scroll_alignment,
          data.vertical_scroll_alignment, data.scroll_behavior);
      break;
    case ax::mojom::Action::kBlur:
    case ax::mojom::Action::kClearAccessibilityFocus:
    case ax::mojom::Action::kDecrement:
    case ax::mojom::Action::kDoDefault:
    case ax::mojom::Action::kIncrement:
    case ax::mojom::Action::kScrollToPoint:
    case ax::mojom::Action::kFocus:
    case ax::mojom::Action::kSetAccessibilityFocus:
    case ax::mojom::Action::kSetScrollOffset:
    case ax::mojom::Action::kSetSequentialFocusNavigationStartingPoint:
    case ax::mojom::Action::kSetValue:
    case ax::mojom::Action::kShowContextMenu:
    case ax::mojom::Action::kScrollBackward:
    case ax::mojom::Action::kScrollForward:
    case ax::mojom::Action::kScrollUp:
    case ax::mojom::Action::kScrollDown:
    case ax::mojom::Action::kScrollLeft:
    case ax::mojom::Action::kScrollRight:
      target->PerformAction(data);
      break;
    case ax::mojom::Action::kCustomAction:
    case ax::mojom::Action::kCollapse:
    case ax::mojom::Action::kExpand:
    case ax::mojom::Action::kHitTest:
    case ax::mojom::Action::kReplaceSelectedText:
    case ax::mojom::Action::kNone:
      NOTREACHED();
      break;
    case ax::mojom::Action::kGetTextLocation:
      break;
    case ax::mojom::Action::kAnnotatePageImages:
      // Ensure we aren't already labeling images, in which case this should
      // not change.
      if (!ax_image_annotator_) {
        CreateAXImageAnnotator();
        // Walk the tree to discover images, and mark them dirty so that
        // they get added to the annotator.
        DCHECK(ax_context_);
        ScopedFreezeAXTreeSource freeze(ax_context_.get());
        ax_context_->MarkAllImageAXObjectsDirty();
      }
      break;
    case ax::mojom::Action::kSignalEndOfTest:
      // Wait for 100ms to allow pending events to come in.
      // TODO(accessibility) Remove sleep() hack; it should no longer be needed.
      base::PlatformThread::Sleep(base::Milliseconds(100));

      HandleAXEvent(
          ui::AXEvent(ComputeRoot().AxID(), ax::mojom::Event::kEndOfTest));
      break;
    case ax::mojom::Action::kShowTooltip:
    case ax::mojom::Action::kHideTooltip:
    case ax::mojom::Action::kInternalInvalidateTree:
    case ax::mojom::Action::kResumeMedia:
    case ax::mojom::Action::kStartDuckingMedia:
    case ax::mojom::Action::kStopDuckingMedia:
    case ax::mojom::Action::kSuspendMedia:
    case ax::mojom::Action::kLongClick:
      break;
  }
  ax_context_->UpdateAXForAllDocuments();
}

void RenderAccessibilityImpl::Reset(int32_t reset_token) {
  reset_token_ = reset_token;
  if (ax_context_) {
    ax_context_->ResetSerializer();
    ax_context_->ClearDirtyObjectsAndPendingEvents();
  }

  const WebDocument& document = GetMainDocument();
  if (!document.IsNull()) {
    // Tree-only mode gets used by the automation extension API which requires a
    // load complete event to invoke listener callbacks.
    // SendPendingAccessibilityEvents() will fire the load complete event
    // if the page is loaded.
    needs_initial_ax_tree_root_ = true;
    event_schedule_mode_ = EventScheduleMode::kProcessEventsImmediately;
    ScheduleSendPendingAccessibilityEvents();
  }
}

void RenderAccessibilityImpl::MarkWebAXObjectDirty(
    const WebAXObject& obj,
    bool subtree,
    ax::mojom::EventFrom event_from,
    ax::mojom::Action event_from_action,
    std::vector<ui::AXEventIntent> event_intents,
    ax::mojom::Event event_type) {
  DCHECK(obj.AccessibilityIsIncludedInTree())
      << "Cannot serialize unincluded object: " << obj.ToString(true).Utf8();

  obj.MarkAXObjectDirtyWithDetails(subtree, event_from, event_from_action,
                                   event_intents);

  NotifyWebAXObjectMarkedDirty(obj, event_type);
}

void RenderAccessibilityImpl::NotifyWebAXObjectMarkedDirty(
  const blink::WebAXObject& obj,
  ax::mojom::Event event_type) {

  // If the event occurred on the focused object, process immediately.
  // kLayoutComplete is an exception because it always fires on the root
  // object but it doesn't imply immediate processing is needed.
  if (obj.IsFocused() && event_type != ax::mojom::Event::kLayoutComplete)
    event_schedule_mode_ = EventScheduleMode::kProcessEventsImmediately;

  ScheduleSendPendingAccessibilityEvents();
}

void RenderAccessibilityImpl::HandleAXEvent(const ui::AXEvent& event) {
  const WebDocument& document = GetMainDocument();
  if (document.IsNull())
    return;

  auto obj = WebAXObject::FromWebDocumentByID(document, event.id);
  if (obj.IsDetached())
    return;

#if BUILDFLAG(IS_ANDROID)
  // Inline text boxes are needed to support moving by character/word/line.
  // On Android, we don't load inline text boxes by default, only on-demand, or
  // when part of the focused object. So, when focus moves to an editable text
  // field, ensure we re-serialize the whole thing including its inline text
  // boxes.
  if (event.event_type == ax::mojom::Event::kFocus && obj.IsEditable())
    obj.InvalidateSerializerSubtree();
#endif

  if (!ax_context_ || !ax_context_->AddPendingEvent(event)) {
    DCHECK(ax_context_);
    return;
  }

  if (IsImmediateProcessingRequiredForEvent(event))
    event_schedule_mode_ = EventScheduleMode::kProcessEventsImmediately;

  if (!obj.IsDetached()) {
    MarkWebAXObjectDirty(obj, /* subtree= */ false, event.event_from,
                         event.event_from_action, event.event_intents,
                         event.event_type);
  }

  ScheduleSendPendingAccessibilityEvents();
}

bool RenderAccessibilityImpl::IsImmediateProcessingRequiredForEvent(
    const ui::AXEvent& event) const {
  if (event_schedule_mode_ == EventScheduleMode::kProcessEventsImmediately)
    return true;  // Already scheduled for immediate mode.

  if (event.event_from == ax::mojom::EventFrom::kAction)
    return true;  // Actions should result in an immediate response.

  switch (event.event_type) {
    case ax::mojom::Event::kActiveDescendantChanged:
    case ax::mojom::Event::kBlur:
    case ax::mojom::Event::kCheckedStateChanged:
    case ax::mojom::Event::kClicked:
    case ax::mojom::Event::kDocumentSelectionChanged:
    case ax::mojom::Event::kFocus:
    case ax::mojom::Event::kHover:
    case ax::mojom::Event::kLoadComplete:
    case ax::mojom::Event::kValueChanged:
      return true;

    case ax::mojom::Event::kAriaAttributeChanged:
    case ax::mojom::Event::kChildrenChanged:
    case ax::mojom::Event::kDocumentTitleChanged:
    case ax::mojom::Event::kExpandedChanged:
    case ax::mojom::Event::kHide:
    case ax::mojom::Event::kLayoutComplete:
    case ax::mojom::Event::kLoadStart:
    case ax::mojom::Event::kLocationChanged:
    case ax::mojom::Event::kMenuListValueChanged:
    case ax::mojom::Event::kRowCollapsed:
    case ax::mojom::Event::kRowCountChanged:
    case ax::mojom::Event::kRowExpanded:
    case ax::mojom::Event::kScrollPositionChanged:
    case ax::mojom::Event::kScrolledToAnchor:
    case ax::mojom::Event::kSelectedChildrenChanged:
    case ax::mojom::Event::kShow:
    case ax::mojom::Event::kTextChanged:
      return false;

    // These events are not fired from Blink.
    // This list is duplicated in WebFrameTestProxy::PostAccessibilityEvent().
    case ax::mojom::Event::kAlert:
    case ax::mojom::Event::kAutocorrectionOccured:
    case ax::mojom::Event::kControlsChanged:
    case ax::mojom::Event::kEndOfTest:
    case ax::mojom::Event::kFocusAfterMenuClose:
    case ax::mojom::Event::kFocusContext:
    case ax::mojom::Event::kHitTestResult:
    case ax::mojom::Event::kImageFrameUpdated:
    case ax::mojom::Event::kLiveRegionCreated:
    case ax::mojom::Event::kLiveRegionChanged:
    case ax::mojom::Event::kMediaStartedPlaying:
    case ax::mojom::Event::kMediaStoppedPlaying:
    case ax::mojom::Event::kMenuEnd:
    case ax::mojom::Event::kMenuPopupEnd:
    case ax::mojom::Event::kMenuPopupStart:
    case ax::mojom::Event::kMenuStart:
    case ax::mojom::Event::kMouseCanceled:
    case ax::mojom::Event::kMouseDragged:
    case ax::mojom::Event::kMouseMoved:
    case ax::mojom::Event::kMousePressed:
    case ax::mojom::Event::kMouseReleased:
    case ax::mojom::Event::kNone:
    case ax::mojom::Event::kSelection:
    case ax::mojom::Event::kSelectionAdd:
    case ax::mojom::Event::kSelectionRemove:
    case ax::mojom::Event::kStateChanged:
    case ax::mojom::Event::kTextSelectionChanged:
    case ax::mojom::Event::kTooltipClosed:
    case ax::mojom::Event::kTooltipOpened:
    case ax::mojom::Event::kTreeChanged:
    case ax::mojom::Event::kWindowActivated:
    case ax::mojom::Event::kWindowDeactivated:
    case ax::mojom::Event::kWindowVisibilityChanged:
      // Never fired from Blink.
      NOTREACHED() << "Event not expected from Blink: " << event.event_type;
      return false;
  }
}

int RenderAccessibilityImpl::GetDeferredEventsDelay() {
  // The amount of time, in milliseconds, to wait before sending non-interactive
  // events that are deferred before the initial page load.
  constexpr int kDelayForDeferredUpdatesBeforePageLoad = 350;

  // The amount of time, in milliseconds, to wait before sending non-interactive
  // events that are deferred after the initial page load.
  // Shync with same constant in CrossPlatformAccessibilityBrowserTest.
  constexpr int kDelayForDeferredUpdatesAfterPageLoad = 150;

  // Prefer WebDocument::IsLoaded() over WebAXObject::IsLoaded() as the
  // latter could trigger a layout update while retrieving the root
  // WebAXObject.
  return GetMainDocument().IsLoaded() ? kDelayForDeferredUpdatesAfterPageLoad
                                      : kDelayForDeferredUpdatesBeforePageLoad;
}

void RenderAccessibilityImpl::ScheduleSendPendingAccessibilityEvents(
    bool scheduling_from_task) {
  // Don't send accessibility events for frames that are not in the frame tree
  // yet (i.e., provisional frames used for remote-to-local navigations, which
  // haven't committed yet).  Doing so might trigger layout, which may not work
  // correctly for those frames.  The events should be sent once such a frame
  // commits.
  if (!render_frame_ || !render_frame_->in_frame_tree())
    return;

  // Don't send accessibility events for frames that don't yet have an tree id
  // as doing so will cause the browser to discard that message and all
  // subsequent ones.
  // TODO(1231184): There are some cases where no content is currently rendered,
  // due to an iframe returning 204 or window.stop() being called. In these
  // cases there will never be an AXTreeID as there is no commit, which will
  // prevent accessibility updates from ever being sent even if the rendering is
  // fixed.
  if (!render_frame_->GetWebFrame()->GetAXTreeID().token())
    return;

  switch (event_schedule_status_) {
    case EventScheduleStatus::kScheduledDeferred:
      if (event_schedule_mode_ ==
          EventScheduleMode::kProcessEventsImmediately) {
        // Cancel scheduled deferred events so we can schedule events to be
        // sent immediately.
        CancelScheduledEvents();
        break;
      }
      // We have already scheduled a task to send pending events.
      return;
    case EventScheduleStatus::kScheduledImmediate:
      // The send pending events task have been scheduled, but has not started.
      return;
    case EventScheduleStatus::kWaitingForAck:
      // Events have been sent, wait for ack.
      return;
    case EventScheduleStatus::kNotWaiting:
      // Once the events have been handled, we schedule the pending events from
      // that task. In this case, there would be a weak ptr still in use.
      if (!scheduling_from_task &&
          weak_factory_for_pending_events_.HasWeakPtrs())
        return;
      break;
  }

  base::TimeDelta delay = base::TimeDelta();
  switch (event_schedule_mode_) {
    case EventScheduleMode::kDeferEvents:
      event_schedule_status_ = EventScheduleStatus::kScheduledDeferred;
      // Where the user is not currently navigating or typing,
      // process changes on a delay so that they occur in larger batches,
      // improving efficiency of repetitive mutations.
      delay = base::Milliseconds(GetDeferredEventsDelay());
      break;
    case EventScheduleMode::kProcessEventsImmediately:
      // This set of events needed to be processed immediately because of a
      // page load or user action.
      event_schedule_status_ = EventScheduleStatus::kScheduledImmediate;
      delay = base::TimeDelta();
      break;
  }

  // When no accessibility events are in-flight post a task to send
  // the events to the browser. We use PostTask so that we can queue
  // up additional events.
  render_frame_->GetTaskRunner(blink::TaskType::kInternalDefault)
      ->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(
              &RenderAccessibilityImpl::SendPendingAccessibilityEvents,
              weak_factory_for_pending_events_.GetWeakPtr()),
          delay);
}

int RenderAccessibilityImpl::GenerateAXID() {
  DCHECK(ax_context_);
  return ax_context_->GenerateAXID();
}

ui::AXTreeID RenderAccessibilityImpl::GetTreeIDForPluginHost() const {
  DCHECK(render_frame_) << "A plugin tree should be under active construction "
                           "only while this render frame is alive.";
  DCHECK(render_frame_->GetWebFrame())
      << "A render frame that contains an actively constructed plugin tree "
         "should be in the list of committed web frames.";
  // TODO(nektar): Why are some frames without an embedding token?
  return render_frame_->GetWebFrame()->GetAXTreeID();
}

void RenderAccessibilityImpl::SetPluginTreeSource(
    PluginAXTreeSource* plugin_tree_source) {
  plugin_tree_source_ = plugin_tree_source;
  plugin_serializer_ =
      std::make_unique<PluginAXTreeSerializer>(plugin_tree_source_);

  OnPluginRootNodeUpdated();
}

void RenderAccessibilityImpl::OnPluginRootNodeUpdated() {
  // Search the accessibility tree for plugin's root object and post a
  // children changed notification on it to force it to update the
  // plugin accessibility tree.
  WebAXObject obj = GetPluginRoot();
  if (obj.IsNull())
    return;

  HandleAXEvent(ui::AXEvent(obj.AxID(), ax::mojom::Event::kChildrenChanged));
}

void RenderAccessibilityImpl::ShowPluginContextMenu() {
  // Search the accessibility tree for plugin's root object and invoke
  // ShowContextMenu() on it to show context menu for plugin.
  WebAXObject obj = GetPluginRoot();
  if (obj.IsNull())
    return;

  const WebDocument& document = GetMainDocument();
  if (document.IsNull())
    return;

  std::unique_ptr<ui::AXActionTarget> target =
      AXActionTargetFactory::CreateFromNodeId(document, plugin_tree_source_,
                                              obj.AxID());
  ui::AXActionData action_data;
  action_data.action = ax::mojom::Action::kShowContextMenu;
  target->PerformAction(action_data);
}

WebDocument RenderAccessibilityImpl::GetMainDocument() {
  if (render_frame_ && render_frame_->GetWebFrame())
    return render_frame_->GetWebFrame()->GetDocument();
  return WebDocument();
}

std::string RenderAccessibilityImpl::GetLanguage() {
  return page_language_;
}

// Ignore code that limits based on the protocol (like https, file, etc.)
// to enable tests to run.
bool g_ignore_protocol_checks_for_testing;

// static
void RenderAccessibilityImpl::IgnoreProtocolChecksForTesting() {
  g_ignore_protocol_checks_for_testing = true;
}

void RenderAccessibilityImpl::AddImageAnnotationsForNode(
    blink::WebAXObject& src,
    ui::AXNodeData* dst) {
  // Images smaller than this number, in CSS pixels, will never get annotated.
  // Note that OCR works on pretty small images, so this shouldn't be too large.
  static const int kMinImageAnnotationWidth = 16;
  static const int kMinImageAnnotationHeight = 16;

  // Reject ignored objects
  if (src.AccessibilityIsIgnored()) {
    return;
  }

  // Reject images that are explicitly empty, or that have a
  // meaningful name already.
  ax::mojom::NameFrom name_from;
  blink::WebVector<WebAXObject> name_objects;
  blink::WebString web_name = src.GetName(name_from, name_objects);

  // If an image has a nonempty name, compute whether we should add an
  // image annotation or not.
  bool should_annotate_image_with_nonempty_name = false;

  // When visual debugging is enabled, the "title" attribute is set to a
  // string beginning with a "%". If the name comes from that string we
  // can ignore it, and treat the name as empty.
  if (image_annotation_debugging_ &&
      base::StartsWith(web_name.Utf8(), "%", base::CompareCase::SENSITIVE))
    should_annotate_image_with_nonempty_name = true;

  if (features::IsAugmentExistingImageLabelsEnabled()) {
    // If the name consists of mostly stopwords, we can add an image
    // annotations. See ax_image_stopwords.h for details.
    if (ax_image_annotator_->ImageNameHasMostlyStopwords(web_name.Utf8()))
      should_annotate_image_with_nonempty_name = true;
  }

  // If the image's name is explicitly empty, or if it has a name (and
  // we're not treating the name as empty), then it's ineligible for
  // an annotation.
  if ((name_from == ax::mojom::NameFrom::kAttributeExplicitlyEmpty ||
       !web_name.IsEmpty()) &&
      !should_annotate_image_with_nonempty_name) {
    dst->SetImageAnnotationStatus(
        ax::mojom::ImageAnnotationStatus::kIneligibleForAnnotation);
    return;
  }

  WebDocument document = render_frame_->GetWebFrame()->GetDocument();

  // If the name of a document (root web area) starts with the filename,
  // it probably means the user opened an image in a new tab.
  // If so, we can treat the name as empty and give it an annotation.
  std::string dst_name =
      dst->GetStringAttribute(ax::mojom::StringAttribute::kName);
  if (ui::IsPlatformDocument(dst->role)) {
    std::string filename = GURL(document.Url()).ExtractFileName();
    if (base::StartsWith(dst_name, filename, base::CompareCase::SENSITIVE))
      should_annotate_image_with_nonempty_name = true;
  }

  // |dst| may be a document or link containing an image. Skip annotating
  // it if it already has text other than whitespace.
  if (!base::ContainsOnlyChars(dst_name, base::kWhitespaceASCII) &&
      !should_annotate_image_with_nonempty_name) {
    dst->SetImageAnnotationStatus(
        ax::mojom::ImageAnnotationStatus::kIneligibleForAnnotation);
    return;
  }

  // Skip images that are too small to label. This also catches
  // unloaded images where the size is unknown.
  WebAXObject offset_container;
  gfx::RectF bounds;
  gfx::Transform container_transform;
  bool clips_children = false;
  src.GetRelativeBounds(offset_container, bounds, container_transform,
                        &clips_children);
  if (bounds.width() < kMinImageAnnotationWidth ||
      bounds.height() < kMinImageAnnotationHeight) {
    dst->SetImageAnnotationStatus(
        ax::mojom::ImageAnnotationStatus::kIneligibleForAnnotation);
    return;
  }

  // Skip images in documents which are not http, https, file and data schemes.
  blink::WebString protocol = document.GetSecurityOrigin().Protocol();
  if (!g_ignore_protocol_checks_for_testing && protocol != url::kHttpScheme &&
      protocol != url::kHttpsScheme && protocol != url::kFileScheme &&
      protocol != url::kDataScheme) {
    dst->SetImageAnnotationStatus(
        ax::mojom::ImageAnnotationStatus::kWillNotAnnotateDueToScheme);
    return;
  }

  // Skip images that do not have an image_src url (e.g. SVGs), or are in
  // documents that do not have a document_url.
  // TODO(accessibility): Remove this check when support for SVGs is added.
  if (!g_ignore_protocol_checks_for_testing &&
      (src.Url().GetString().Utf8().empty() ||
       document.Url().GetString().Utf8().empty()))
    return;

  if (!ax_image_annotator_) {
    if (!first_unlabeled_image_id_.has_value() ||
        first_unlabeled_image_id_.value() == src.AxID()) {
      dst->SetImageAnnotationStatus(
          ax::mojom::ImageAnnotationStatus::kEligibleForAnnotation);
      first_unlabeled_image_id_ = src.AxID();
    } else {
      dst->SetImageAnnotationStatus(
          ax::mojom::ImageAnnotationStatus::kSilentlyEligibleForAnnotation);
    }
    return;
  }

  if (ax_image_annotator_->HasAnnotationInCache(src)) {
    dst->AddStringAttribute(ax::mojom::StringAttribute::kImageAnnotation,
                            ax_image_annotator_->GetImageAnnotation(src));
    dst->SetImageAnnotationStatus(
        ax_image_annotator_->GetImageAnnotationStatus(src));
  } else if (ax_image_annotator_->HasImageInCache(src)) {
    ax_image_annotator_->OnImageUpdated(src);
    dst->SetImageAnnotationStatus(
        ax::mojom::ImageAnnotationStatus::kAnnotationPending);
  } else if (!ax_image_annotator_->HasImageInCache(src)) {
    ax_image_annotator_->OnImageAdded(src);
    dst->SetImageAnnotationStatus(
        ax::mojom::ImageAnnotationStatus::kAnnotationPending);
  }
}

// Helper function that searches in the subtree of |obj| to a max
// depth of |max_depth| for an image.
//
// Returns true on success, or false if it finds more than one image,
// or any node with a name, or anything deeper than |max_depth|.
bool SearchForExactlyOneInnerImage(WebAXObject obj,
                                   WebAXObject* inner_image,
                                   int max_depth) {
  DCHECK(inner_image);

  // If it's the first image, set |inner_image|. If we already
  // found an image, fail.
  if (ui::IsImage(obj.Role())) {
    if (!inner_image->IsDetached())
      return false;
    *inner_image = obj;
  } else {
    // If we found something else with a name, fail.
    if (!ui::IsPlatformDocument(obj.Role()) && !ui::IsLink(obj.Role())) {
      blink::WebString web_name = obj.GetName();
      if (!base::ContainsOnlyChars(web_name.Utf8(), base::kWhitespaceASCII)) {
        return false;
      }
    }
  }

  // Fail if we recursed to |max_depth| and there's more of a subtree.
  if (max_depth == 0 && obj.ChildCount())
    return false;

  // Don't count ignored nodes toward depth.
  int next_depth = obj.AccessibilityIsIgnored() ? max_depth : max_depth - 1;

  // Recurse.
  for (unsigned int i = 0; i < obj.ChildCount(); i++) {
    if (!SearchForExactlyOneInnerImage(obj.ChildAt(i), inner_image, next_depth))
      return false;
  }

  return !inner_image->IsDetached();
}

// Return true if the subtree of |obj|, to a max depth of 3, contains
// exactly one image. Return that image in |inner_image|.
bool FindExactlyOneInnerImageInMaxDepthThree(WebAXObject obj,
                                             WebAXObject* inner_image) {
  DCHECK(inner_image);
  return SearchForExactlyOneInnerImage(obj, inner_image, /* max_depth = */ 3);
}

void RenderAccessibilityImpl::AddImageAnnotations(
    const WebDocument& document,
    std::vector<ui::AXNodeData>& nodes) {
  if (accessibility_mode_.has_mode(ui::AXMode::kPDF))
    return;
  for (auto& node : nodes) {
    WebAXObject src = WebAXObject::FromWebDocumentByID(document, node.id);

    // This logic is equivalent to the early-outs in
    // BlinkAXTreeSource::SerializeNode
    if (src.IsDetached() || !src.AccessibilityIsIncludedInTree() ||
        (src.AccessibilityIsIgnored() &&
         !node.HasState(ax::mojom::State::kFocusable)))
      continue;

    if (ui::IsImage(node.role)) {
      AddImageAnnotationsForNode(src, &node);
    } else if ((ui::IsLink(node.role) || ui::IsPlatformDocument(node.role)) &&
               node.GetNameFrom() != ax::mojom::NameFrom::kAttribute) {
      WebAXObject inner_image;
      if (FindExactlyOneInnerImageInMaxDepthThree(src, &inner_image))
        AddImageAnnotationsForNode(inner_image, &node);
    }
  }
}

bool RenderAccessibilityImpl::SerializeUpdatesAndEvents(
    WebDocument document,
    WebAXObject root,
    std::vector<ui::AXEvent>& events,
    std::vector<ui::AXTreeUpdate>& updates,
    bool invalidate_plugin_subtree) {
  bool had_end_of_test_event = false;

  // If there's a layout complete or a scroll changed message, we need to send
  // location changes.
  bool need_to_send_location_changes = false;

  // Keep track of load complete messages. When a load completes, it's a good
  // time to inject a stylesheet for image annotation debugging.
  bool had_load_complete_messages = false;

  // Serialize all dirty objects in the list at this point in time, stopping
  // either when the queue is empty, or the number of remaining objects to
  // serialize has been reached.
  ax_context_->SerializeDirtyObjectsAndEvents(
      !!plugin_tree_source_, updates, events, had_end_of_test_event,
      had_load_complete_messages, need_to_send_location_changes);

  for (auto& update : updates) {
    if (update.node_id_to_clear > 0)
      invalidate_plugin_subtree = true;

    if (plugin_tree_source_)
      AddPluginTreeToUpdate(&update, invalidate_plugin_subtree);

    AddImageAnnotations(document, update.nodes);
  }

  if (had_end_of_test_event) {
    ui::AXEvent end_of_test(root.AxID(), ax::mojom::Event::kEndOfTest);
    if (!WebAXObject::IsDirty(document)) {
      events.emplace_back(end_of_test);
    } else {
      // Document is still dirty, queue up another end of test and process
      // immediately.
      event_schedule_mode_ = EventScheduleMode::kProcessEventsImmediately;
      HandleAXEvent(end_of_test);
    }
  }

  if (had_load_complete_messages) {
    has_injected_stylesheet_ = false;
  }

  return need_to_send_location_changes;
}

void RenderAccessibilityImpl::SendPendingAccessibilityEvents() {
  TRACE_EVENT0("accessibility",
               "RenderAccessibilityImpl::SendPendingAccessibilityEvents");
  base::ElapsedTimer timer;

  // Clear status here in case we return early.
  event_schedule_status_ = EventScheduleStatus::kNotWaiting;
  WebDocument document = GetMainDocument();
  if (document.IsNull())
    return;

  // Don't serialize child trees without an embedding token. These are
  // unrendered child frames. This prevents a situation where child trees can't
  // be linked to their parent, leading to a dangerous situation for some
  // platforms, where events are fired on objects not connected to the root. For
  // example, on Mac, this can lead to a lockup in AppKit.
  CHECK(document.GetFrame()->GetEmbeddingToken());

  DCHECK(document.IsAccessibilityEnabled())
      << "SendPendingAccessibilityEvents should not do any work when nothing "
         "has enabled accessibility.";

  // TODO(aleventhal): needs_initial_ax_tree_root_ and this whole piece of logic
  // will eventually either go away or move to AXObjectCacheImpl, where it can
  // be done more simply. Basically we want to fire a page load event when
  // waking up and the page was already loaded.
  if (needs_initial_ax_tree_root_) {
    // At the very start of accessibility for this document, push a layout
    // complete for the entire document, in order to initialize the browser's
    // cached accessibility tree.
    needs_initial_ax_tree_root_ = false;
    ax_context_->UpdateAXForAllDocuments();
    auto root_obj = WebAXObject::FromWebDocument(document);
    // Always fire layout complete for a new root object.
    // TODO(aleventhal): eventually hopefully we can get rid of
    // insert_at_beginning. We only need that for inserting the fake load event
    // when we wake up to an already-loaded page. But it would be better to
    // just insert the kLoadComplete event when creating the root and the page
    // was already loaded.
    ax_context_->AddPendingEvent(
        ui::AXEvent(root_obj.AxID(), ax::mojom::Event::kLayoutComplete),
        true /* insert_at_beginning*/);
    MarkWebAXObjectDirty(root_obj, false);

    // If loaded and has some content, insert load complete at the top, so that
    // screen readers are informed a new document is ready.
    // This is helpful in the case where the screen reader is launched after the
    // page was already loaded.
    if (root_obj.IsLoaded() && !document.Body().IsNull() &&
        !document.Body().FirstChild().IsNull()) {
      ax_context_->AddPendingEvent(
          ui::AXEvent(root_obj.AxID(), ax::mojom::Event::kLoadComplete),
          true /* insert_at_beginning*/);
    }
  }

  if (!ax_context_->HasDirtyObjects()) {
    // By default, assume the next batch does not have interactive events, and
    // defer so that the batch of events is larger. If any interactive events
    // come in, the batch will be processed immediately.
    event_schedule_mode_ = EventScheduleMode::kDeferEvents;
    return;
  }

  // Update layout before snapshotting the events so that live state read from
  // the DOM during freezing (e.g. which node currently has focus) is consistent
  // with the events and node data we're about to send up.
  ax_context_->UpdateAXForAllDocuments();

  ScopedFreezeAXTreeSource freeze(ax_context_.get());
  WebAXObject root = ComputeRoot();
#if DCHECK_IS_ON()
  // Never causes a document lifecycle change during serialization,
  // because the assumption is that layout is in a safe, stable state.
  // (Skip if image_annotation_debugging_ is enabled because it adds
  // style attributes to images, affecting the document lifecycle
  // during accessibility.)
  std::unique_ptr<blink::WebDisallowTransitionScope> disallow;
  if (!image_annotation_debugging_)
    disallow = std::make_unique<blink::WebDisallowTransitionScope>(&document);
#endif

  // Save the page language.
  page_language_ = root.Language().Utf8();

#if DCHECK_IS_ON()
  // Protect against lifecycle changes in the popup document, if any.
  WebDocument popup_document = GetPopupDocument();
  std::unique_ptr<blink::WebDisallowTransitionScope> disallow2;
  if (!image_annotation_debugging_ && !popup_document.IsNull()) {
    disallow2 =
        std::make_unique<blink::WebDisallowTransitionScope>(&popup_document);
  }
#endif

  // Keep track of if the host node for a plugin has been invalidated,
  // because if so, the plugin subtree will need to be re-serialized.
  bool invalidate_plugin_subtree = false;
  if (plugin_tree_source_) {
    invalidate_plugin_subtree = !plugin_host_node_.IsInClientTree();
  }

  // The serialized list of updates and events to send to the browser.
  blink::mojom::AXUpdatesAndEventsPtr updates_and_events =
      blink::mojom::AXUpdatesAndEvents::New();

  bool need_to_send_location_changes = SerializeUpdatesAndEvents(
      document, root, updates_and_events->events, updates_and_events->updates,
      invalidate_plugin_subtree);
  if (updates_and_events->updates.empty()) {
    // Do not send a serialization if there are no updates.
    DCHECK(updates_and_events->events.empty())
        << "If there are no updates, there also shouldn't be any events, "
           "because events always mark an object dirty.";
    return;
  }

  if (image_annotation_debugging_)
    AddImageAnnotationDebuggingAttributes(updates_and_events->updates);

  event_schedule_status_ = EventScheduleStatus::kWaitingForAck;
  render_accessibility_manager_->HandleAccessibilityEvents(
      std::move(updates_and_events), reset_token_,
      base::BindOnce(&RenderAccessibilityImpl::OnAccessibilityEventsHandled,
                     weak_factory_for_pending_events_.GetWeakPtr()));
  reset_token_ = 0;

  if (need_to_send_location_changes)
    SendLocationChanges();

  // Now that this batch is complete, assume the next batch does not have
  // interactive events, and defer so that the batch of events is larger.
  // If any interactive events come in, the batch will be processed immediately.
  event_schedule_mode_ = EventScheduleMode::kDeferEvents;

  if (features::IsAblateSendPendingAccessibilityEventsEnabled()) {
    // Make the total time equal to 2x the original time.
    auto new_end_time = base::Time::Now() + timer.Elapsed();
    while (base::Time::Now() < new_end_time) {
      // spin loop.
    }
  }

  // Measure the amount of time spent in this function. Keep track of the
  // maximum within a time interval so we can upload UKM.
  base::TimeDelta elapsed_time_ms = timer.Elapsed();
  if (elapsed_time_ms > slowest_serialization_time_) {
    last_ukm_source_id_ = document.GetUkmSourceId();
    slowest_serialization_time_ = elapsed_time_ms;
  }
  // Also log the time taken in this function to track serialization
  // performance.
  UMA_HISTOGRAM_TIMES(
      "Accessibility.Performance.SendPendingAccessibilityEvents",
      elapsed_time_ms);

  if (ukm_timer_->Elapsed() >= kMinUKMDelay)
    MaybeSendUKM();
}

void RenderAccessibilityImpl::SendLocationChanges() {
  TRACE_EVENT0("accessibility", "RenderAccessibilityImpl::SendLocationChanges");
  ax_context_->SerializeLocationChanges();
}

void RenderAccessibilityImpl::OnAccessibilityEventsHandled() {
  DCHECK_EQ(event_schedule_status_, EventScheduleStatus::kWaitingForAck);
  event_schedule_status_ = EventScheduleStatus::kNotWaiting;
  switch (event_schedule_mode_) {
    case EventScheduleMode::kDeferEvents:
      ScheduleSendPendingAccessibilityEvents(true);
      break;
    case EventScheduleMode::kProcessEventsImmediately:
      SendPendingAccessibilityEvents();
      break;
  }
}

void RenderAccessibilityImpl::OnLoadInlineTextBoxes(
    const ui::AXActionTarget* target) {
  const BlinkAXActionTarget* blink_target =
      BlinkAXActionTarget::FromAXActionTarget(target);
  if (!blink_target)
    return;
  const WebAXObject& obj = blink_target->WebAXObject();

  ScopedFreezeAXTreeSource freeze(ax_context_.get());
  obj.OnLoadInlineTextBoxes();

  // Explicitly send a tree change update event now.
  event_schedule_mode_ = EventScheduleMode::kProcessEventsImmediately;
  HandleAXEvent(ui::AXEvent(obj.AxID(), ax::mojom::Event::kTreeChanged));
}

void RenderAccessibilityImpl::OnGetImageData(const ui::AXActionTarget* target,
                                             const gfx::Size& max_size) {
  const BlinkAXActionTarget* blink_target =
      BlinkAXActionTarget::FromAXActionTarget(target);
  if (!blink_target)
    return;
  const WebAXObject& obj = blink_target->WebAXObject();

  ScopedFreezeAXTreeSource freeze(ax_context_.get());
  if (obj.ImageDataNodeId() == obj.AxID())
    return;

  obj.SetImageAsDataNodeId(max_size);

  const WebDocument& document = GetMainDocument();
  if (document.IsNull())
    return;

  obj.InvalidateSerializerSubtree();
  event_schedule_mode_ = EventScheduleMode::kProcessEventsImmediately;
  HandleAXEvent(ui::AXEvent(obj.AxID(), ax::mojom::Event::kImageFrameUpdated));
}

void RenderAccessibilityImpl::OnDestruct() {
  render_frame_ = nullptr;
  delete this;
}

void RenderAccessibilityImpl::AddPluginTreeToUpdate(
    ui::AXTreeUpdate* update,
    bool invalidate_plugin_subtree) {
  const WebDocument& document = GetMainDocument();
  if (invalidate_plugin_subtree)
    plugin_serializer_->Reset();

  for (ui::AXNodeData& node : update->nodes) {
    if (node.role == ax::mojom::Role::kEmbeddedObject) {
      plugin_host_node_ = WebAXObject::FromWebDocumentByID(document, node.id);

      const ui::AXNode* root = plugin_tree_source_->GetRoot();
      node.child_ids.push_back(root->id());

      ui::AXTreeUpdate plugin_update;
      plugin_serializer_->SerializeChanges(root, &plugin_update);

      size_t old_count = update->nodes.size();
      size_t new_count = plugin_update.nodes.size();
      update->nodes.resize(old_count + new_count);
      for (size_t i = 0; i < new_count; ++i)
        update->nodes[old_count + i] = plugin_update.nodes[i];
      break;
    }
  }

  if (plugin_tree_source_->GetTreeData(&update->tree_data))
    update->has_tree_data = true;
}

void RenderAccessibilityImpl::CreateAXImageAnnotator() {
  if (!render_frame_)
    return;
  mojo::PendingRemote<image_annotation::mojom::Annotator> annotator;
  render_frame_->GetBrowserInterfaceBroker()->GetInterface(
      annotator.InitWithNewPipeAndPassReceiver());

  ax_image_annotator_ =
      std::make_unique<AXImageAnnotator>(this, std::move(annotator));
}

void RenderAccessibilityImpl::StartOrStopLabelingImages(ui::AXMode old_mode,
                                                        ui::AXMode new_mode) {
  if (!render_frame_)
    return;

  if (!old_mode.has_mode(ui::AXMode::kLabelImages) &&
      new_mode.has_mode(ui::AXMode::kLabelImages)) {
    CreateAXImageAnnotator();
  } else if (old_mode.has_mode(ui::AXMode::kLabelImages) &&
             !new_mode.has_mode(ui::AXMode::kLabelImages)) {
    ax_image_annotator_->Destroy();
    ax_image_annotator_.reset();
  }
}

void RenderAccessibilityImpl::AddImageAnnotationDebuggingAttributes(
    const std::vector<ui::AXTreeUpdate>& updates) {
  DCHECK(image_annotation_debugging_);

  for (auto& update : updates) {
    for (auto& node : update.nodes) {
      if (!node.HasIntAttribute(
              ax::mojom::IntAttribute::kImageAnnotationStatus))
        continue;

      ax::mojom::ImageAnnotationStatus status = node.GetImageAnnotationStatus();
      bool should_set_attributes = false;
      switch (status) {
        case ax::mojom::ImageAnnotationStatus::kNone:
        case ax::mojom::ImageAnnotationStatus::kWillNotAnnotateDueToScheme:
        case ax::mojom::ImageAnnotationStatus::kIneligibleForAnnotation:
        case ax::mojom::ImageAnnotationStatus::kEligibleForAnnotation:
        case ax::mojom::ImageAnnotationStatus::kSilentlyEligibleForAnnotation:
          break;
        case ax::mojom::ImageAnnotationStatus::kAnnotationPending:
        case ax::mojom::ImageAnnotationStatus::kAnnotationAdult:
        case ax::mojom::ImageAnnotationStatus::kAnnotationEmpty:
        case ax::mojom::ImageAnnotationStatus::kAnnotationProcessFailed:
        case ax::mojom::ImageAnnotationStatus::kAnnotationSucceeded:
          should_set_attributes = true;
          break;
      }

      if (!should_set_attributes)
        continue;

      WebDocument document = GetMainDocument();
      if (document.IsNull())
        continue;
      WebAXObject obj = WebAXObject::FromWebDocumentByID(document, node.id);
      if (obj.IsDetached())
        continue;

      if (!has_injected_stylesheet_) {
        document.InsertStyleSheet(
            "[imageannotation=annotationPending] { outline: 3px solid #9ff; } "
            "[imageannotation=annotationSucceeded] { outline: 3px solid #3c3; "
            "} "
            "[imageannotation=annotationEmpty] { outline: 3px solid #ee6; } "
            "[imageannotation=annotationAdult] { outline: 3px solid #f90; } "
            "[imageannotation=annotationProcessFailed] { outline: 3px solid "
            "#c00; } ");
        has_injected_stylesheet_ = true;
      }

      WebNode web_node = obj.GetNode();
      if (web_node.IsNull() || !web_node.IsElementNode())
        continue;

      WebElement element = web_node.To<WebElement>();
      std::string status_str = ui::ToString(status);
      if (element.GetAttribute("imageannotation").Utf8() != status_str)
        element.SetAttribute("imageannotation",
                             blink::WebString::FromUTF8(status_str));

      std::string title = "%" + status_str;
      std::string annotation =
          node.GetStringAttribute(ax::mojom::StringAttribute::kImageAnnotation);
      if (!annotation.empty())
        title = title + ": " + annotation;
      if (element.GetAttribute("title").Utf8() != title) {
        element.SetAttribute("title", blink::WebString::FromUTF8(title));
      }
    }
  }
}

blink::WebDocument RenderAccessibilityImpl::GetPopupDocument() {
  blink::WebPagePopup* popup = render_frame_->GetWebView()->GetPagePopup();
  if (popup)
    return popup->GetDocument();
  return WebDocument();
}

absl::optional<gfx::RectF> RenderAccessibilityImpl::GetPopupBounds() {
  const WebDocument& popup_document = GetPopupDocument();
  if (popup_document.IsNull())
    return absl::nullopt;

  auto obj = WebAXObject::FromWebDocument(popup_document);

  gfx::RectF popup_bounds;
  WebAXObject popup_container;
  gfx::Transform transform;
  obj.GetRelativeBounds(popup_container, popup_bounds, transform);

  // The |popup_container| will never be set for a popup element. See
  // `AXObject::GetRelativeBounds`.
  DCHECK(popup_container.IsNull());

  return popup_bounds;
}

blink::WebAXObject RenderAccessibilityImpl::GetPluginRoot() {
  if (!ax_context_)
    return WebAXObject();
  ax_context_->UpdateAXForAllDocuments();
  ScopedFreezeAXTreeSource freeze(ax_context_.get());
  return ax_context_->GetPluginRoot();
}

WebAXObject RenderAccessibilityImpl::ComputeRoot() {
  DCHECK(render_frame_);
  DCHECK(render_frame_->GetWebFrame());
  return WebAXObject::FromWebDocument(GetMainDocument());
}

void RenderAccessibilityImpl::CancelScheduledEvents() {
  switch (event_schedule_status_) {
    case EventScheduleStatus::kScheduledDeferred:
    case EventScheduleStatus::kScheduledImmediate:  // Fallthrough
      weak_factory_for_pending_events_.InvalidateWeakPtrs();
      event_schedule_status_ = EventScheduleStatus::kNotWaiting;
      break;
    case EventScheduleStatus::kWaitingForAck:
    case EventScheduleStatus::kNotWaiting:  // Fallthrough
      break;
  }
}

void RenderAccessibilityImpl::ConnectionClosed() {
  event_schedule_status_ = EventScheduleStatus::kNotWaiting;
}

void RenderAccessibilityImpl::MaybeSendUKM() {
  if (slowest_serialization_time_ < kMinSerializationTimeToSend)
    return;

  ukm::builders::Accessibility_Renderer(last_ukm_source_id_)
      .SetCpuTime_SendPendingAccessibilityEvents(
          slowest_serialization_time_.InMilliseconds())
      .Record(ukm_recorder_.get());
  ResetUKMData();
}

void RenderAccessibilityImpl::ResetUKMData() {
  slowest_serialization_time_ = base::TimeDelta();
  ukm_timer_ = std::make_unique<base::ElapsedTimer>();
  last_ukm_source_id_ = ukm::kInvalidSourceId;
}

}  // namespace content
