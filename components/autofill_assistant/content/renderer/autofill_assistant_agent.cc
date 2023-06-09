// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill_assistant/content/renderer/autofill_assistant_agent.h"

#include <ostream>

#include "base/command_line.h"
#include "components/autofill_assistant/content/common/switches.h"
#include "components/optimization_guide/machine_learning_tflite_buildflags.h"
#include "content/public/renderer/render_frame.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/platform/web_vector.h"
#include "third_party/blink/public/web/modules/autofill_assistant/node_signals.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_form_control_element.h"
#include "third_party/blink/public/web/web_input_element.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/protobuf/src/google/protobuf/repeated_field.h"

#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
#include "components/autofill_assistant/content/common/proto/semantic_feature_overrides.pb.h"
#include "components/autofill_assistant/content/renderer/autofill_assistant_agent_debug_utils.h"
#include "components/autofill_assistant/content/renderer/autofill_assistant_model_executor.h"
#endif  // BUILDFLAG(BUILD_WITH_TFLITE_LIB)

namespace autofill_assistant {
namespace {

#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)

constexpr char16_t kSemanticPredictionAttributeName[] = u"semantic-prediction";

using OverridesMap = AutofillAssistantModelExecutor::OverridesMap;
using SparseVector = AutofillAssistantModelExecutor::SparseVector;

SparseVector KeyCoordinatesToSparseVector(
    const ::google::protobuf::RepeatedPtrField<SparseEncoding>&
        key_coordinates) {
  SparseVector sparse_vector;
  for (const auto& coordinate : key_coordinates) {
    sparse_vector.emplace_back(
        std::make_pair(std::make_pair(coordinate.feature_concatenation_index(),
                                      coordinate.vocabulary_index()),
                       coordinate.number_of_occurrences()));
  }
  return sparse_vector;
}

absl::optional<OverridesMap> ParseOverridesPolicyToMap(
    std::string overrides_policy) {
  SemanticSelectorPolicy policy;
  if (!policy.ParseFromString(
          std::string(overrides_policy.begin(), overrides_policy.end()))) {
    return absl::nullopt;
  }
  if (policy.bag_of_words().data_point_map().empty()) {
    return absl::nullopt;
  }
  OverridesMap overrides_map;
  for (const auto& data_point : policy.bag_of_words().data_point_map()) {
    if (data_point.key_coordinate().empty()) {
      continue;
    }
    const auto& value = data_point.value();
    overrides_map[KeyCoordinatesToSparseVector(data_point.key_coordinate())] =
        std::make_pair(value.semantic_role(), value.objective());
  }
  return overrides_map;
}

#endif  // BUILDFLAG(BUILD_WITH_TFLITE_LIB)

}  // namespace

AutofillAssistantAgent::AutofillAssistantAgent(
    content::RenderFrame* render_frame,
    blink::AssociatedInterfaceRegistry* registry)
    : content::RenderFrameObserver(render_frame) {
  registry->AddInterface<mojom::AutofillAssistantAgent>(base::BindRepeating(
      &AutofillAssistantAgent::BindPendingReceiver, base::Unretained(this)));

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kAutofillAssistantDebugAnnotateDom)) {
    std::string jsonEncodedSemanticEnums =
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
            autofill_assistant::switches::kAutofillAssistantDebugAnnotateDom);
#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
    semantic_labels =
        DecodeSemanticPredictionLabelsJson(jsonEncodedSemanticEnums);
#endif
  }
}

// The destructor is not guaranteed to be called. Destruction happens (only)
// through the OnDestruct() event, which posts a task to delete this object.
// The process may be killed before this deletion can happen.
AutofillAssistantAgent::~AutofillAssistantAgent() = default;

void AutofillAssistantAgent::BindPendingReceiver(
    mojo::PendingAssociatedReceiver<mojom::AutofillAssistantAgent>
        pending_receiver) {
  receiver_.Bind(std::move(pending_receiver));
  receiver_.reset_on_disconnect();
}

void AutofillAssistantAgent::OnDestruct() {
  delete this;
}

base::WeakPtr<AutofillAssistantAgent> AutofillAssistantAgent::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void AutofillAssistantAgent::GetSemanticNodes(
    int32_t role,
    int32_t objective,
    bool ignore_objective,
    base::TimeDelta model_timeout,
    GetSemanticNodesCallback callback) {
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  if (!frame) {
    VLOG(1) << "Failed to get semantic nodes, no frame.";
    std::move(callback).Run(mojom::NodeDataStatus::kUnexpectedError,
                            std::vector<NodeData>());
    return;
  }

  GetAnnotateDomModel(
      model_timeout,
      base::BindOnce(&AutofillAssistantAgent::OnGetModelFile,
                     weak_ptr_factory_.GetWeakPtr(), base::Time::Now(), frame,
                     role, objective, ignore_objective, std::move(callback)));
}

void AutofillAssistantAgent::GetAnnotateDomModel(
    base::TimeDelta model_timeout,
    base::OnceCallback<void(mojom::ModelStatus, base::File, const std::string&)>
        callback) {
  mojo::AssociatedRemote<mojom::AutofillAssistantDriver>& driver = GetDriver();

  if (!driver || !driver.is_connected()) {
    std::move(callback).Run(mojom::ModelStatus::kUnexpectedError, base::File(),
                            /*overrides_policy=*/"");
    return;
  }
  driver->GetAnnotateDomModel(model_timeout, std::move(callback));
}

mojo::AssociatedRemote<mojom::AutofillAssistantDriver>&
AutofillAssistantAgent::GetDriver() {
  if (!driver_) {
    render_frame()->GetRemoteAssociatedInterfaces()->GetInterface(&driver_);
    driver_.reset_on_disconnect();
  }
  return driver_;
}

void AutofillAssistantAgent::OnGetModelFile(
    base::Time start_time,
    blink::WebLocalFrame* frame,
    int32_t role,
    int32_t objective,
    bool ignore_objective,
    GetSemanticNodesCallback callback,
    mojom::ModelStatus model_status,
    base::File model,
    const std::string& overrides_policy) {
  std::vector<NodeData> nodes;
  switch (model_status) {
    case mojom::ModelStatus::kSuccess:
      break;
    case mojom::ModelStatus::kUnexpectedError:
      std::move(callback).Run(mojom::NodeDataStatus::kModelLoadError, nodes);
      return;
    case mojom::ModelStatus::kTimeout:
      std::move(callback).Run(mojom::NodeDataStatus::kModelLoadTimeout, nodes);
      return;
  }

  base::Time on_get_model_file = base::Time::Now();
  DVLOG(3) << "AutofillAssistant, loading model file: "
           << (on_get_model_file - start_time).InMilliseconds() << "ms";

  blink::WebVector<blink::AutofillAssistantNodeSignals> node_signals =
      blink::GetAutofillAssistantNodeSignals(frame->GetDocument());

  base::Time on_node_signals = base::Time::Now();
  DVLOG(3) << "AutofillAssistant, signals extraction: "
           << (on_node_signals - on_get_model_file).InMilliseconds() << "ms";

#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
  AutofillAssistantModelExecutor model_executor(
      ParseOverridesPolicyToMap(std::move(overrides_policy)));
  if (!model_executor.InitializeModelFromFile(std::move(model))) {
    std::move(callback).Run(mojom::NodeDataStatus::kInitializationError, nodes);
    return;
  }

  base::Time on_executor_initialized = base::Time::Now();
  DVLOG(3) << "AutofillAssistant, executor initialization: "
           << (on_executor_initialized - on_node_signals).InMilliseconds()
           << "ms";
  DVLOG(3) << "Expected role: " << role << " and objective: " << objective;

  for (const auto& node_signal : node_signals) {
    auto result = model_executor.ExecuteModelWithInput(node_signal);
    if (base::CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kAutofillAssistantDebugAnnotateDom)) {
      VLOG(3) << NodeSignalsToDebugString(node_signal);
      if (result) {
        std::u16string result_string = SemanticPredictionResultToDebugString(
            semantic_labels.first, semantic_labels.second, result.value(),
            ignore_objective);
        VLOG(3) << "Result found " << result_string;

        SetElementAttribute(node_signal.backend_node_id,
                            kSemanticPredictionAttributeName, result_string,
                            /*send_events=*/false);
      }
    }

    if (result && result->role == role &&
        (result->objective == objective || ignore_objective)) {
      NodeData node_data;
      node_data.backend_node_id = node_signal.backend_node_id;
      node_data.used_override = result->used_override;
      nodes.push_back(node_data);
    }
  }

  base::Time on_node_signals_evaluated = base::Time::Now();
  DVLOG(3)
      << "AutofillAssistant, node evaluation (for " << node_signals.size()
      << " nodes): "
      << (on_node_signals_evaluated - on_executor_initialized).InMilliseconds()
      << "ms";
#endif  // BUILDFLAG(BUILD_WITH_TFLITE_LIB)

  std::move(callback).Run(mojom::NodeDataStatus::kSuccess, nodes);
}

void AutofillAssistantAgent::SetElementValue(const int32_t backend_node_id,
                                             const std::u16string& value,
                                             bool send_events,
                                             SetElementValueCallback callback) {
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  if (!frame) {
    VLOG(1) << "Failed to set Element value, no frame.";
    std::move(callback).Run(false);
    return;
  }

  blink::WebElement target_element =
      frame->GetDocument().GetElementByDevToolsNodeId(backend_node_id);
  if (target_element.IsNull() || !target_element.IsFormControlElement()) {
    VLOG(3) << "Failed to set Element value, invalid target.";
    std::move(callback).Run(false);
    return;
  }

  blink::WebFormControlElement target_form_control_element =
      target_element.To<blink::WebFormControlElement>();
  target_form_control_element.SetValue(blink::WebString::FromUTF16(value),
                                       send_events);
  std::move(callback).Run(true);
}

void AutofillAssistantAgent::SetElementAttribute(
    const int32_t backend_node_id,
    const std::u16string& attribute_name,
    const std::u16string& value,
    bool send_events) {
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  if (!frame) {
    VLOG(1) << "Failed to set Element attribute, no frame.";
    return;
  }

  blink::WebElement target_element =
      frame->GetDocument().GetElementByDevToolsNodeId(backend_node_id);
  if (target_element.IsNull() || !target_element.IsFormControlElement()) {
    VLOG(3) << "Failed to set Element attribute, invalid target.";
    return;
  }

  blink::WebFormControlElement target_form_control_element =
      target_element.To<blink::WebFormControlElement>();
  target_form_control_element.SetAttribute(
      blink::WebString::FromUTF16(attribute_name),
      blink::WebString::FromUTF16(value));
}

void AutofillAssistantAgent::SetElementChecked(
    const int32_t backend_node_id,
    bool checked,
    bool send_events,
    SetElementCheckedCallback callback) {
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  if (!frame) {
    VLOG(1) << "Failed to set Element checked state, no frame.";
    std::move(callback).Run(false);
    return;
  }

  blink::WebElement target_element =
      frame->GetDocument().GetElementByDevToolsNodeId(backend_node_id);
  if (target_element.IsNull()) {
    VLOG(3) << "Failed to set Element checked state, invalid target.";
    std::move(callback).Run(false);
    return;
  }

  blink::WebInputElement web_input_element =
      target_element.DynamicTo<blink::WebInputElement>();
  if (web_input_element.IsNull()) {
    VLOG(3) << "Failed to set Element checked state, target is not an input "
               "element.";
    std::move(callback).Run(false);
    return;
  }

  if (!web_input_element.IsCheckbox() && !web_input_element.IsRadioButton()) {
    VLOG(3) << "Failed to set Element checked state, target is not a checkbox "
               "or a radio button.";
    std::move(callback).Run(false);
    return;
  }

  web_input_element.SetChecked(checked, send_events);
  std::move(callback).Run(true);
}

}  // namespace autofill_assistant
