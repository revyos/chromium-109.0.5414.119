// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/input_method/autocorrect_manager.h"

#include "ash/constants/ash_features.h"
#include "base/callback_helpers.h"
#include "base/feature_list.h"
#include "base/i18n/case_conversion.h"
#include "base/metrics/histogram_functions.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/ash/input_method/assistive_window_properties.h"
#include "chrome/browser/ash/input_method/autocorrect_enums.h"
#include "chrome/browser/ash/input_method/ime_rules_config.h"
#include "chrome/browser/ash/input_method/suggestion_enums.h"
#include "chrome/browser/ui/ash/keyboard/chrome_keyboard_controller_client.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "ui/base/ime/ash/extension_ime_util.h"
#include "ui/base/ime/ash/ime_bridge.h"
#include "ui/base/ime/ash/input_method_manager.h"
#include "ui/base/ime/ash/text_input_target.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/events/keycodes/dom/dom_code.h"

namespace ash {
namespace input_method {

namespace {

constexpr int kMaxEditDistance = 30;
constexpr int kDistanceUntilUnderlineHides = 3;
constexpr int kMaxValidationTries = 4;
constexpr base::TimeDelta kVeryFastInteractionPeriod = base::Milliseconds(200);
constexpr base::TimeDelta kFastInteractionPeriod = base::Milliseconds(500);

bool IsVkAutocorrect() {
  return ChromeKeyboardControllerClient::HasInstance() &&
         ChromeKeyboardControllerClient::Get()->is_keyboard_visible();
}

bool IsCurrentInputMethodExperimentalMultilingual() {
  auto* input_method_manager = InputMethodManager::Get();
  if (!input_method_manager) {
    return false;
  }
  return extension_ime_util::IsExperimentalMultilingual(
      input_method_manager->GetActiveIMEState()->GetCurrentInputMethod().id());
}

bool IsUsEnglishId(const std::string& engine_id) {
  return engine_id == "xkb:us::eng";
}

AutocorrectPreference GetPhysicalKeyboardAutocorrectPref(
    PrefService* prefs,
    const std::string& engine_id) {
  const base::Value::Dict& input_method_settings =
      prefs->GetDict(::prefs::kLanguageInputMethodSpecificSettings);
  const base::Value* autocorrect_setting =
      input_method_settings.FindByDottedPath(
          engine_id + ".physicalKeyboardAutoCorrectionLevel");

  if (!autocorrect_setting)
    return AutocorrectPreference::kDefault;
  if (!autocorrect_setting->GetIfInt().has_value())
    return AutocorrectPreference::kDefault;
  if (autocorrect_setting->GetIfInt().value() > 0)
    return AutocorrectPreference::kEnabled;
  return AutocorrectPreference::kDisabled;
}

AutocorrectCompatibilitySummary ConvertActionToCompatibilitySummary(
    AutocorrectActions action) {
  switch (action) {
    case AutocorrectActions::kWindowShown:
      return AutocorrectCompatibilitySummary::kWindowShown;
    case AutocorrectActions::kUnderlined:
      return AutocorrectCompatibilitySummary::kUnderlined;
    case AutocorrectActions::kReverted:
      return AutocorrectCompatibilitySummary::kReverted;
    case AutocorrectActions::kUserAcceptedAutocorrect:
      return AutocorrectCompatibilitySummary::kUserAcceptedAutocorrect;
    case AutocorrectActions::kUserActionClearedUnderline:
      return AutocorrectCompatibilitySummary::kUserActionClearedUnderline;
    case AutocorrectActions::kUserExitedTextFieldWithUnderline:
      return AutocorrectCompatibilitySummary::kUserExitedTextFieldWithUnderline;
    case AutocorrectActions::kInvalidRange:
      return AutocorrectCompatibilitySummary::kInvalidRange;
    default:
      LOG(ERROR) << "Invalid AutocorrectActions: action=" << (int)action;
      return AutocorrectCompatibilitySummary::kInvalidRange;
  }
}

void RecordAppCompatibilityUkm(
    ukm::SourceId source_id,
    bool virtual_keyboard,
    AutocorrectCompatibilitySummary compatibility_summary) {
  if (virtual_keyboard) {
    ukm::builders::InputMethod_Assistive_AutocorrectV2(source_id)
        .SetCompatibilitySummary_VK(static_cast<int>(compatibility_summary))
        .Record(ukm::UkmRecorder::Get());
  } else {
    ukm::builders::InputMethod_Assistive_AutocorrectV2(source_id)
        .SetCompatibilitySummary_PK(static_cast<int>(compatibility_summary))
        .Record(ukm::UkmRecorder::Get());
  }
}

void LogAutocorrectAppCompatibilityUkm(AutocorrectActions action,
                                       base::TimeDelta time_delta,
                                       bool virtual_keyboard_visible) {
  ui::TextInputTarget* input_context =
      ui::IMEBridge::Get()->GetInputContextHandler();
  if (!input_context) {
    return;
  }

  ukm::SourceId sourceId = input_context->GetClientSourceForMetrics();
  if (sourceId == ukm::kInvalidSourceId) {
    return;
  }

  // Record base interactions.
  RecordAppCompatibilityUkm(sourceId, virtual_keyboard_visible,
                            ConvertActionToCompatibilitySummary(action));

  if (time_delta > kFastInteractionPeriod) {
    return;
  }

  bool is_very_fast = time_delta <= kVeryFastInteractionPeriod;

  AutocorrectCompatibilitySummary latency_compatibility;

  // Convert latency of important interaction to CompatibilitySummary.
  switch (action) {
    case AutocorrectActions::kUserAcceptedAutocorrect:
      latency_compatibility =
          is_very_fast
              ? AutocorrectCompatibilitySummary::kVeryFastAcceptedAutocorrect
              : AutocorrectCompatibilitySummary::kFastAcceptedAutocorrect;
      break;
    case AutocorrectActions::kReverted:
    case AutocorrectActions::kUserActionClearedUnderline:
    case AutocorrectActions::kInvalidRange:
      latency_compatibility =
          is_very_fast
              ? AutocorrectCompatibilitySummary::kVeryFastRejectedAutocorrect
              : AutocorrectCompatibilitySummary::kFastRejectedAutocorrect;
      break;
    case AutocorrectActions::kUserExitedTextFieldWithUnderline:
      latency_compatibility =
          is_very_fast ? AutocorrectCompatibilitySummary::kVeryFastExitField
                       : AutocorrectCompatibilitySummary::kFastExitField;
      break;
    default:
      return;
  }

  RecordAppCompatibilityUkm(sourceId, virtual_keyboard_visible,
                            latency_compatibility);
}

void LogAssistiveAutocorrectDelay(base::TimeDelta delay) {
  base::UmaHistogramMediumTimes("InputMethod.Assistive.Autocorrect.Delay",
                                delay);
  if (IsCurrentInputMethodExperimentalMultilingual()) {
    base::UmaHistogramMediumTimes(
        "InputMethod.MultilingualExperiment.Autocorrect.Delay", delay);
  }
}

void LogAssistiveAutocorrectActionLatency(AutocorrectActions action,
                                          base::TimeDelta time_delta,
                                          bool virtual_keyboard_visible) {
  switch (action) {
    case AutocorrectActions::kUnderlined:
    case AutocorrectActions::kWindowShown:
      // Skip non-terminal actions.
      return;
    case AutocorrectActions::kUserAcceptedAutocorrect:
      base::UmaHistogramMediumTimes(
          "InputMethod.Assistive.AutocorrectV2.Latency.Accept", time_delta);
      break;
    case AutocorrectActions::kReverted:
    case AutocorrectActions::kUserActionClearedUnderline:
    case AutocorrectActions::kInvalidRange:
      base::UmaHistogramMediumTimes(
          "InputMethod.Assistive.AutocorrectV2.Latency.Reject", time_delta);
      break;
    case AutocorrectActions::kUserExitedTextFieldWithUnderline:
      base::UmaHistogramMediumTimes(
          "InputMethod.Assistive.AutocorrectV2.Latency.ExitField", time_delta);
      break;
    default:
      LOG(ERROR) << "Invalid AutocorrectActions: action=" << (int)action;
      return;
  }

  // Record the duration of the pending autocorrect for VK and PK.
  if (virtual_keyboard_visible) {
    base::UmaHistogramMediumTimes(
        "InputMethod.Assistive.AutocorrectV2.Latency.VkPending", time_delta);
  } else {
    base::UmaHistogramMediumTimes(
        "InputMethod.Assistive.AutocorrectV2.Latency.PkPending", time_delta);
  }
}

void LogAssistiveAutocorrectInternalState(
    AutocorrectInternalStates internal_state) {
  if (IsVkAutocorrect()) {
    base::UmaHistogramEnumeration(
        "InputMethod.Assistive.AutocorrectV2.Internal.VkState", internal_state);
  } else {
    base::UmaHistogramEnumeration(
        "InputMethod.Assistive.AutocorrectV2.Internal.PkState", internal_state);
  }
}

void LogAssistiveAutocorrectQualityBreakdown(
    AutocorrectQualityBreakdown quality_breakdown,
    bool suggestion_accepted,
    bool virtual_keyboard_visible) {
  std::string histogram_name = "InputMethod.Assistive.AutocorrectV2.Quality.";

  // Explicitly use autocorrect histogram name so that this usage can be found
  // using code search.
  if (virtual_keyboard_visible) {
    if (suggestion_accepted) {
      base::UmaHistogramEnumeration(
          "InputMethod.Assistive.AutocorrectV2.Quality.VkAccepted",
          quality_breakdown);
    } else {
      base::UmaHistogramEnumeration(
          "InputMethod.Assistive.AutocorrectV2.Quality.VkRejected",
          quality_breakdown);
    }
  } else {
    if (suggestion_accepted) {
      base::UmaHistogramEnumeration(
          "InputMethod.Assistive.AutocorrectV2.Quality.PkAccepted",
          quality_breakdown);
    } else {
      base::UmaHistogramEnumeration(
          "InputMethod.Assistive.AutocorrectV2.Quality.PkRejected",
          quality_breakdown);
    }
  }
}

// Returns the Levenshtein distance between |str1| and |str2|.
// Which is the minimum number of single-character edits (i.e. insertions,
// deletions or substitutions) required to change one word into the other.
// https://en.wikipedia.org/wiki/Levenshtein_distance
int GetLevenshteinDistance(const std::u16string& str1,
                           const std::u16string& str2) {
  if (str1.size() > str2.size()) {
    return GetLevenshteinDistance(str2, str1);
  }
  if (str1.size() + static_cast<size_t>(kMaxEditDistance) < str2.size()) {
    return kMaxEditDistance;
  }

  std::vector<int> row(str1.size() + 1);
  for (size_t i = 0; i < row.size(); ++i) {
    row[i] = static_cast<int>(i);
  }

  for (size_t i = 0; i < str2.size(); ++i) {
    ++row[0];
    int previous = static_cast<int>(i);
    bool under_cutoff = false;
    for (size_t j = 0; j < str1.size(); ++j) {
      int old_row = row[j + 1];
      int cost = str2[i] == str1[j] ? 0 : 1;
      row[j + 1] = std::min(std::min(row[j], row[j + 1]) + 1, previous + cost);
      if (row[j + 1] < kMaxEditDistance) {
        under_cutoff = true;
      }
      previous = old_row;
    }

    if (!under_cutoff) {
      return kMaxEditDistance;
    }
  }
  return row[str1.size()];
}

void MeasureAndLogAssistiveAutocorrectEditDistance(
    const std::u16string& original_text,
    const std::u16string& suggested_text,
    const bool suggestion_accepted,
    const bool virtual_keyboard_visible) {
  const int text_length =
      std::min(static_cast<int>(original_text.length()), kMaxEditDistance);
  const int distance = std::min(
      GetLevenshteinDistance(original_text, suggested_text), kMaxEditDistance);
  if (text_length <= 0 || distance <= 0) {
    return;
  }

  const std::string histogram_base_name =
      "InputMethod.Assistive.AutocorrectV2.Distance.";
  std::string keyboard_type_extension =
      virtual_keyboard_visible ? ".Vk" : ".Pk";
  keyboard_type_extension += suggestion_accepted ? "Accepted" : "Rejected";

  // This is a 2d array of size (kMaxEditDistance x kMaxEditDistance) that
  // has been flattened
  const int flattenedLengthVsDistance =
      (text_length - 1) * kMaxEditDistance + distance - 1;
  base::UmaHistogramSparse(
      base::StrCat({histogram_base_name, "OriginalLengthVsLevenshteinDistance",
                    keyboard_type_extension}),
      flattenedLengthVsDistance);
  base::UmaHistogramExactLinear(
      base::StrCat(
          {histogram_base_name, "SuggestedLength", keyboard_type_extension}),
      suggested_text.length(), kMaxEditDistance);
}

void RecordAssistiveCoverage(AssistiveType type) {
  base::UmaHistogramEnumeration("InputMethod.Assistive.Coverage", type);
}

void RecordAssistiveSuccess(AssistiveType type) {
  base::UmaHistogramEnumeration("InputMethod.Assistive.Success", type);
}

void RecordPhysicalKeyboardAutocorrectPref(const std::string& engine_id,
                                           const AutocorrectPreference& pref) {
  if (IsUsEnglishId(engine_id)) {
    base::UmaHistogramEnumeration(
        "InputMethod.Assistive.AutocorrectV2.PkUserPreference.English", pref);
  }
  base::UmaHistogramEnumeration(
      "InputMethod.Assistive.AutocorrectV2.PkUserPreference.All", pref);
}

bool CouldTriggerAutocorrectWithSurroundingText(const std::u16string& text,
                                                size_t cursor_pos,
                                                size_t anchor_pos) {
  // TODO(b/161490813): Do not count cases that autocorrect is disabled.
  //    Currently, there are different logics in different places that disable
  //    autocorrect based on settings, domain and text field attributes.
  //    Ideally, all the cases that autocorrect is disabled on a text field
  //    must not be counted here.
  return cursor_pos == anchor_pos && cursor_pos == text.size() &&
         text.size() >= 2 && base::IsAsciiWhitespace(text.back()) &&
         !base::IsAsciiWhitespace(text[text.size() - 2]);
}

bool IsAutocorrectSuggestionInSurroundingText(
    const std::u16string& surrounding_text,
    const gfx::Range& autocorrect_range,
    const std::u16string& suggested_text) {
  if (autocorrect_range.is_empty() ||
      suggested_text.length() != autocorrect_range.length() ||
      autocorrect_range.end() > surrounding_text.length()) {
    return false;
  }

  return surrounding_text.substr(autocorrect_range.start(),
                                 autocorrect_range.length()) == suggested_text;
}

}  // namespace

AutocorrectManager::AutocorrectManager(
    SuggestionHandlerInterface* suggestion_handler,
    Profile* profile)
    : suggestion_handler_(suggestion_handler), profile_(profile) {}

AutocorrectManager::~AutocorrectManager() = default;

void AutocorrectManager::HandleAutocorrect(const gfx::Range autocorrect_range,
                                           const std::u16string& original_text,
                                           const std::u16string& current_text) {
  ++num_handled_autocorrect_in_text_field_;

  if (DisabledByRule()) {
    LogAssistiveAutocorrectInternalState(
        AutocorrectInternalStates::kHandleSuggestionInDenylistedApp);
  }

  // TODO(crbug/1111135): call setAutocorrectTime() (for metrics)
  // TODO(crbug/1111135): record metric (coverage)
  ui::TextInputTarget* input_context =
      ui::IMEBridge::Get()->GetInputContextHandler();
  if (!input_context) {
    LogAssistiveAutocorrectInternalState(
        AutocorrectInternalStates::kHandleNoInputContext);
    AcceptOrClearPendingAutocorrect();
    return;
  }

  if (pending_autocorrect_.has_value()) {
    LogAssistiveAutocorrectInternalState(
        AutocorrectInternalStates::kHandleUnclearedRange);
    AcceptOrClearPendingAutocorrect();
  }

  if (autocorrect_range.is_empty() ||
      autocorrect_range.length() != current_text.length() ||
      original_text.empty()) {
    if (autocorrect_range.is_empty()) {
      LogAssistiveAutocorrectInternalState(
          AutocorrectInternalStates::kHandleEmptyRange);
    } else {
      LogAssistiveAutocorrectInternalState(
          AutocorrectInternalStates::kHandleInvalidArgs);
    }
    input_context->SetAutocorrectRange(gfx::Range(), base::DoNothing());
    return;
  }

  LogAssistiveAutocorrectInternalState(
      AutocorrectInternalStates::kHandleSetRange);

  input_context->SetAutocorrectRange(
      autocorrect_range,
      base::BindOnce(&AutocorrectManager::ProcessSetAutocorrectRangeDone,
                     weak_ptr_factory_.GetWeakPtr(), autocorrect_range,
                     original_text, current_text));  // show underline
}

void AutocorrectManager::ProcessSetAutocorrectRangeDone(
    const gfx::Range& autocorrect_range,
    const std::u16string& original_text,
    const std::u16string& current_text,
    bool set_range_success) {
  if (!set_range_success) {
    LogAssistiveAutocorrectInternalState(
        AutocorrectInternalStates::kErrorSetRange);
    return;
  }

  in_diacritical_autocorrect_session_ =
      IsCurrentInputMethodExperimentalMultilingual() &&
      diacritics_insensitive_string_comparator_.Equal(original_text,
                                                      current_text);

  pending_autocorrect_ = AutocorrectManager::PendingAutocorrectState(
      /*original_text=*/original_text, /*suggested_text=*/current_text,
      /*start_time=*/base::TimeTicks::Now(),
      /*virtual_keyboard_visible=*/IsVkAutocorrect());

  LogAssistiveAutocorrectInternalState(
      AutocorrectInternalStates::kUnderlineShown);

  LogAssistiveAutocorrectAction(AutocorrectActions::kUnderlined);
  RecordAssistiveCoverage(AssistiveType::kAutocorrectUnderlined);
}

void AutocorrectManager::LogAssistiveAutocorrectAction(
    AutocorrectActions action) {
  base::UmaHistogramEnumeration("InputMethod.Assistive.Autocorrect.Actions",
                                action);

  if (pending_autocorrect_.has_value()) {
    base::TimeDelta latency =
        base::TimeTicks::Now() - pending_autocorrect_->start_time;
    LogAssistiveAutocorrectActionLatency(
        action, latency, pending_autocorrect_->virtual_keyboard_visible);

    LogAutocorrectAppCompatibilityUkm(
        action, latency, pending_autocorrect_->virtual_keyboard_visible);
  }

  if (pending_autocorrect_.has_value() &&
      pending_autocorrect_->virtual_keyboard_visible) {
    base::UmaHistogramEnumeration(
        "InputMethod.Assistive.Autocorrect.Actions.VK", action);
    base::UmaHistogramEnumeration(
        "InputMethod.Assistive.AutocorrectV2.Actions.VK", action);
  } else {
    base::UmaHistogramEnumeration(
        "InputMethod.Assistive.AutocorrectV2.Actions.PK", action);
  }

  if (IsCurrentInputMethodExperimentalMultilingual()) {
    base::UmaHistogramEnumeration(
        "InputMethod.MultilingualExperiment.Autocorrect.Actions", action);

    if (in_diacritical_autocorrect_session_) {
      base::UmaHistogramEnumeration(
          "InputMethod.MultilingualExperiment.DiacriticalAutocorrect.Actions",
          action);
    }
  }
}

void AutocorrectManager::MeasureAndLogAssistiveAutocorrectQualityBreakdown(
    AutocorrectActions action) {
  if (!pending_autocorrect_.has_value() ||
      pending_autocorrect_->suggested_text.empty() ||
      pending_autocorrect_->original_text.empty() ||
      (action != AutocorrectActions::kUserAcceptedAutocorrect &&
       action != AutocorrectActions::kUserActionClearedUnderline &&
       action != AutocorrectActions::kReverted)) {
    return;
  }

  bool suggestion_accepted =
      action == AutocorrectActions::kUserAcceptedAutocorrect;
  bool virtual_keyboard_visible =
      pending_autocorrect_->virtual_keyboard_visible;

  const std::u16string& original_text = pending_autocorrect_->original_text;
  const std::u16string& suggested_text = pending_autocorrect_->suggested_text;

  LogAssistiveAutocorrectQualityBreakdown(
      AutocorrectQualityBreakdown::kSuggestionResolved, suggestion_accepted,
      virtual_keyboard_visible);
  MeasureAndLogAssistiveAutocorrectEditDistance(original_text, suggested_text,
                                                suggestion_accepted,
                                                virtual_keyboard_visible);

  if (diacritics_insensitive_string_comparator_.Equal(original_text,
                                                      suggested_text)) {
    LogAssistiveAutocorrectQualityBreakdown(
        AutocorrectQualityBreakdown::kSuggestionChangedAccent,
        suggestion_accepted, virtual_keyboard_visible);
  }

  if (suggested_text.find(' ') != std::u16string::npos) {
    LogAssistiveAutocorrectQualityBreakdown(
        AutocorrectQualityBreakdown::kSuggestionSplittedWord,
        suggestion_accepted, virtual_keyboard_visible);
  }

  if (original_text.size() < suggested_text.size()) {
    LogAssistiveAutocorrectQualityBreakdown(
        AutocorrectQualityBreakdown::kSuggestionInsertedLetters,
        suggestion_accepted, virtual_keyboard_visible);
  } else if (original_text.size() == suggested_text.size()) {
    LogAssistiveAutocorrectQualityBreakdown(
        AutocorrectQualityBreakdown::kSuggestionMutatedLetters,
        suggestion_accepted, virtual_keyboard_visible);
  } else {
    LogAssistiveAutocorrectQualityBreakdown(
        AutocorrectQualityBreakdown::kSuggestionRemovedLetters,
        suggestion_accepted, virtual_keyboard_visible);
  }

  if (base::i18n::ToLower(original_text) ==
      base::i18n::ToLower(suggested_text)) {
    LogAssistiveAutocorrectQualityBreakdown(
        AutocorrectQualityBreakdown::kSuggestionChangeLetterCases,
        suggestion_accepted, virtual_keyboard_visible);
  }

  if (base::IsAsciiLower(original_text[0]) &&
      base::IsAsciiUpper(suggested_text[0])) {
    LogAssistiveAutocorrectQualityBreakdown(
        AutocorrectQualityBreakdown::kSuggestionCapitalizedWord,
        suggestion_accepted, virtual_keyboard_visible);
  } else if (base::IsAsciiUpper(original_text[0]) &&
             base::IsAsciiLower(suggested_text[0])) {
    LogAssistiveAutocorrectQualityBreakdown(
        AutocorrectQualityBreakdown::kSuggestionLowerCasedWord,
        suggestion_accepted, virtual_keyboard_visible);
  }

  if (base::IsStringASCII(original_text)) {
    LogAssistiveAutocorrectQualityBreakdown(
        AutocorrectQualityBreakdown::kOriginalTextIsAscii, suggestion_accepted,
        virtual_keyboard_visible);
  }
  if (base::IsStringASCII(suggested_text)) {
    LogAssistiveAutocorrectQualityBreakdown(
        AutocorrectQualityBreakdown::kSuggestedTextIsAscii, suggestion_accepted,
        virtual_keyboard_visible);
  }
}

void AutocorrectManager::OnActivate(const std::string& engine_id) {
  active_engine_id_ = engine_id;
}

bool AutocorrectManager::OnKeyEvent(const ui::KeyEvent& event) {
  if (pending_user_pref_metric_ && IsVkAutocorrect()) {
    // We only want to record a pending user pref metric if the user is
    // currently using the physical keyboard.
    pending_user_pref_metric_ = absl::nullopt;
  }

  if (pending_user_pref_metric_) {
    const std::string& engine_id = pending_user_pref_metric_->engine_id;
    RecordPhysicalKeyboardAutocorrectPref(
        engine_id,
        GetPhysicalKeyboardAutocorrectPref(profile_->GetPrefs(), engine_id));
    pending_user_pref_metric_ = absl::nullopt;
  }

  // OnKeyEvent is only used for interacting with the undo UI.
  if (!pending_autocorrect_.has_value() ||
      !pending_autocorrect_->undo_window_visible ||
      event.type() != ui::ET_KEY_PRESSED) {
    return false;
  }

  if (event.code() == ui::DomCode::ARROW_UP ||
      event.code() == ui::DomCode::TAB) {
    HighlightUndoButton();
    return true;
  }
  if (event.code() == ui::DomCode::ENTER &&
      pending_autocorrect_->undo_button_highlighted) {
    UndoAutocorrect();
    return true;
  }

  return false;
}

void AutocorrectManager::OnSurroundingTextChanged(const std::u16string& text,
                                                  const int cursor_pos,
                                                  const int anchor_pos) {
  if (error_on_hiding_undo_window_) {
    HideUndoWindow();
  }

  if (CouldTriggerAutocorrectWithSurroundingText(text, cursor_pos,
                                                 anchor_pos)) {
    LogAssistiveAutocorrectInternalState(
        AutocorrectInternalStates::kCouldTriggerAutocorrect);
  }

  if (!pending_autocorrect_.has_value()) {
    return;
  }

  std::string error;
  ui::TextInputTarget* input_context =
      ui::IMEBridge::Get()->GetInputContextHandler();

  // Null input context invalidates the range so consider the pending
  // range as implicitly rejected/cleared.
  if (!input_context) {
    AcceptOrClearPendingAutocorrect();
    return;
  }

  if (!pending_autocorrect_->is_validated) {
    // Validate that the surrounding text matches with pending autocorrect
    // suggestion. Because of delays in update of surrounding text and
    // autocorrect range, the validation waits until all these information are
    // matching with each others (a.k.a. updated). This is necessary for
    // implementation of autocorrect interactions such as implicit acceptance.
    pending_autocorrect_->is_validated =
        IsAutocorrectSuggestionInSurroundingText(
            text, input_context->GetAutocorrectRange(),
            pending_autocorrect_->suggested_text);
    pending_autocorrect_->validation_tries++;

    if (!pending_autocorrect_->is_validated) {
      // Clear suggestion if multiple trials of validation fails.
      // This is a guard to prevent unwanted situation that can keep
      // autocorrect suggestion pending forever.
      if (pending_autocorrect_->validation_tries >= kMaxValidationTries) {
        AcceptOrClearPendingAutocorrect();
      }
      return;
    }
  }

  const gfx::Range range = input_context->GetAutocorrectRange();
  const uint32_t cursor_pos_unsigned
      = base::checked_cast<uint32_t>(cursor_pos);

  // If range is empty, it means user has mutated suggestion. So, clear range
  // and consider autocorrect suggestion as implicitly rejected.
  if (range.is_empty()) {
    AcceptOrClearPendingAutocorrect();
    return;
  }

  // If it is the first call of the event after handling autocorrect range,
  // initialize the variables and do not process the empty range as it is
  // potentially stale.
  if (pending_autocorrect_->num_inserted_chars < 0) {
    pending_autocorrect_->num_inserted_chars = 0;
  } else if (static_cast<int>(text.length()) >
             pending_autocorrect_->text_length) {
    // TODO(b/161490813): Fix double counting of emojis and some CJK chars.
    // TODO(b/161490813): Fix logic for text replace.

    // Count characters added between two calls of the event.
    pending_autocorrect_->num_inserted_chars += text.length() -
        pending_autocorrect_->text_length;
  }
  pending_autocorrect_->text_length = text.length();

  // If the number of added characters after setting the pending range is above
  // the threshold, then accept the pending range.
  if (pending_autocorrect_->num_inserted_chars >=
      kDistanceUntilUnderlineHides) {
    AcceptOrClearPendingAutocorrect();
    return;
  }

  // If cursor is inside autocorrect range (inclusive), show undo window and
  // record relevant metrics.
  if (cursor_pos_unsigned >= range.start() &&
      cursor_pos_unsigned <= range.end() && cursor_pos == anchor_pos) {
    ShowUndoWindow(range, text);
  } else {
    // Ensure undo window is hidden when cursor is not inside the autocorrect
    // range.
    HideUndoWindow();
  }
}

void AutocorrectManager::OnFocus(int context_id) {
  if (active_engine_id_) {
    pending_user_pref_metric_ =
        PendingPhysicalKeyboardUserPrefMetric{.engine_id = *active_engine_id_};
  }

  if (base::FeatureList::IsEnabled(ash::features::kImeRuleConfig)) {
    GetTextFieldContextualInfo(
        base::BindOnce(&AutocorrectManager::OnTextFieldContextualInfoChanged,
                       base::Unretained(this)));
  }

  num_handled_autocorrect_in_text_field_ = 0;

  LogAssistiveAutocorrectInternalState(
      AutocorrectInternalStates::kOnFocusEvent);
  if (pending_autocorrect_.has_value()) {
    LogAssistiveAutocorrectInternalState(
        AutocorrectInternalStates::kOnFocusEventWithPendingSuggestion);
  }

  context_id_ = context_id;
  ProcessTextFieldChange();
}

void AutocorrectManager::OnBlur() {
  LogAssistiveAutocorrectInternalState(AutocorrectInternalStates::kOnBlurEvent);

  if (pending_autocorrect_.has_value()) {
    LogAssistiveAutocorrectInternalState(
        AutocorrectInternalStates::kOnBlurEventWithPendingSuggestion);
  }

  if (num_handled_autocorrect_in_text_field_ > 0) {
    LogAssistiveAutocorrectInternalState(
        AutocorrectInternalStates::kTextFieldEditsWithAtLeastOneSuggestion);
  }

  ProcessTextFieldChange();
}

void AutocorrectManager::ProcessTextFieldChange() {
  ui::TextInputTarget* input_context =
      ui::IMEBridge::Get()->GetInputContextHandler();

  // Clear autocorrect range if any.
  if (input_context) {
    HideUndoWindow();
    input_context->SetAutocorrectRange(gfx::Range(), base::DoNothing());
  }

  if (pending_autocorrect_.has_value()) {
    LogAssistiveAutocorrectAction(
        AutocorrectActions::kUserExitedTextFieldWithUnderline);
    pending_autocorrect_.reset();
  }
}

void AutocorrectManager::UndoAutocorrect() {
  if (!pending_autocorrect_.has_value() ||
      !pending_autocorrect_->is_validated) {
    return;
  }

  HideUndoWindow();

  ui::TextInputTarget* input_context =
      ui::IMEBridge::Get()->GetInputContextHandler();
  const gfx::Range autocorrect_range = input_context->GetAutocorrectRange();

  if (input_context->HasCompositionText()) {
    input_context->SetComposingRange(autocorrect_range.start(),
                                     autocorrect_range.end(), {});
    input_context->CommitText(
        pending_autocorrect_->original_text,
        ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  } else {
    // NOTE: GetSurroundingTextInfo() could return a stale cache that no longer
    // reflects reality, due to async-ness between IMF and TextInputClient.
    // TODO(crbug/1194424): Work around the issue or fix
    // GetSurroundingTextInfo().
    const ui::SurroundingTextInfo surrounding_text =
        input_context->GetSurroundingTextInfo();

    // Delete the autocorrected text.
    // This will not quite work properly if there is text actually highlighted,
    // and cursor is at end of the highlight block, but no easy way around it.
    // First delete everything before cursor.
    DCHECK(autocorrect_range.Contains(surrounding_text.selection_range));
    const uint32_t before =
        surrounding_text.selection_range.start() - autocorrect_range.start();
    const uint32_t after =
        autocorrect_range.end() - surrounding_text.selection_range.end();
    input_context->DeleteSurroundingText(before, after);

    // Replace with the original text.
    input_context->CommitText(
        pending_autocorrect_->original_text,
        ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  }

  MeasureAndLogAssistiveAutocorrectQualityBreakdown(
      AutocorrectActions::kReverted);
  LogAssistiveAutocorrectAction(AutocorrectActions::kReverted);
  RecordAssistiveCoverage(AssistiveType::kAutocorrectReverted);
  RecordAssistiveSuccess(AssistiveType::kAutocorrectReverted);
  LogAssistiveAutocorrectDelay(
    base::TimeTicks::Now() - pending_autocorrect_->start_time);

  pending_autocorrect_.reset();
}

void AutocorrectManager::ShowUndoWindow(
  gfx::Range range, const std::u16string& text) {
  if (!pending_autocorrect_.has_value() ||
      !pending_autocorrect_->is_validated ||
      pending_autocorrect_->undo_window_visible) {
    return;
  }

  std::string error;
  const std::u16string autocorrected_text =
      text.substr(range.start(), range.length());
  AssistiveWindowProperties properties;
  properties.type = ash::ime::AssistiveWindowType::kUndoWindow;
  properties.visible = true;
  properties.announce_string = l10n_util::GetStringFUTF16(
      IDS_SUGGESTION_AUTOCORRECT_UNDO_WINDOW_SHOWN,
      pending_autocorrect_->original_text,
      autocorrected_text);
  suggestion_handler_->SetAssistiveWindowProperties(context_id_, properties,
                                                    &error);

  LogAssistiveAutocorrectInternalState(
      AutocorrectInternalStates::kShowUndoWindow);

  if (!error.empty()) {
    LogAssistiveAutocorrectInternalState(
        AutocorrectInternalStates::kErrorShowUndoWindow);
    LOG(ERROR) << "Failed to show autocorrect undo window.";
    return;
  }

  // Showing a new undo window overrides the current shown undo window. So
  // there is no need to first trying to hide the previous one.
  error_on_hiding_undo_window_ = false;

  if (!pending_autocorrect_->window_shown_logged) {
    LogAssistiveAutocorrectAction(AutocorrectActions::kWindowShown);
    RecordAssistiveCoverage(AssistiveType::kAutocorrectWindowShown);
    pending_autocorrect_->window_shown_logged = true;
  }

  pending_autocorrect_->undo_button_highlighted = false;
  pending_autocorrect_->undo_window_visible = true;
}

void AutocorrectManager::HideUndoWindow() {
  if (!error_on_hiding_undo_window_ &&
      (!pending_autocorrect_.has_value() ||
       !pending_autocorrect_->undo_window_visible)) {
    return;
  }

  std::string error;
  AssistiveWindowProperties properties;
  properties.type = ash::ime::AssistiveWindowType::kUndoWindow;
  properties.visible = false;
  suggestion_handler_->SetAssistiveWindowProperties(context_id_, properties,
                                                    &error);

  LogAssistiveAutocorrectInternalState(
      AutocorrectInternalStates::kHideUndoWindow);

  if (!error.empty()) {
    LogAssistiveAutocorrectInternalState(
        AutocorrectInternalStates::kErrorHideUndoWindow);
    LOG(ERROR) << "Failed to hide autocorrect undo window.";
    error_on_hiding_undo_window_ = true;
    return;
  }

  error_on_hiding_undo_window_ = false;

  if (pending_autocorrect_.has_value()) {
    pending_autocorrect_->undo_button_highlighted = false;
    pending_autocorrect_->undo_window_visible = false;
  }
}

void AutocorrectManager::HighlightUndoButton() {
  if (!pending_autocorrect_.has_value() ||
      !pending_autocorrect_->undo_window_visible ||
      pending_autocorrect_->undo_button_highlighted) {
    return;
  }

  std::string error;
  ui::ime::AssistiveWindowButton button = ui::ime::AssistiveWindowButton();
  button.id = ui::ime::ButtonId::kUndo;
  button.window_type = ash::ime::AssistiveWindowType::kUndoWindow;
  button.announce_string = l10n_util::GetStringFUTF16(
      IDS_SUGGESTION_AUTOCORRECT_UNDO_BUTTON,
      pending_autocorrect_->original_text);
  suggestion_handler_->SetButtonHighlighted(context_id_, button, true,
                                            &error);

  LogAssistiveAutocorrectInternalState(
      AutocorrectInternalStates::kHighlightUndoWindow);

  if (!error.empty()) {
    LOG(ERROR) << "Failed to highlight undo button.";
    return;
  }

  pending_autocorrect_->undo_button_highlighted = true;
}

void AutocorrectManager::AcceptOrClearPendingAutocorrect() {
  if (!pending_autocorrect_.has_value()) {
    return;
  }

  ui::TextInputTarget* input_context =
      ui::IMEBridge::Get()->GetInputContextHandler();

  LogAssistiveAutocorrectInternalState(
      AutocorrectInternalStates::kSuggestionResolved);

  if (!input_context) {
    LogAssistiveAutocorrectAction(AutocorrectActions::kInvalidRange);
    LogAssistiveAutocorrectInternalState(
        AutocorrectInternalStates::kNoInputContext);
  } else if (!pending_autocorrect_->is_validated) {
    LogAssistiveAutocorrectAction(AutocorrectActions::kInvalidRange);
    LogAssistiveAutocorrectInternalState(
        AutocorrectInternalStates::kErrorRangeNotValidated);
  } else if (!input_context->GetAutocorrectRange().is_empty()) {
    MeasureAndLogAssistiveAutocorrectQualityBreakdown(
        AutocorrectActions::kUserAcceptedAutocorrect);
    LogAssistiveAutocorrectInternalState(
        AutocorrectInternalStates::kSuggestionAccepted);
    // Non-empty autocorrect range means that the user has not modified
    // autocorrect suggestion to invalidate it. So, it is considered as
    // accepted.
    LogAssistiveAutocorrectAction(
      AutocorrectActions::kUserAcceptedAutocorrect);
  } else {
    MeasureAndLogAssistiveAutocorrectQualityBreakdown(
        AutocorrectActions::kUserActionClearedUnderline);
    LogAssistiveAutocorrectAction(
      AutocorrectActions::kUserActionClearedUnderline);
  }

  if (input_context) {
    input_context->SetAutocorrectRange(gfx::Range(),
                                       base::DoNothing());  // clear underline
  }

  HideUndoWindow();
  pending_autocorrect_.reset();
}

void AutocorrectManager::OnTextFieldContextualInfoChanged(
    const TextFieldContextualInfo& info) {
  disabled_by_rule_ =
      ImeRulesConfig::GetInstance()->IsAutoCorrectDisabled(info);
  if (disabled_by_rule_) {
    LogAssistiveAutocorrectInternalState(
        AutocorrectInternalStates::kAppIsInDenylist);
  }
}

bool AutocorrectManager::DisabledByRule() {
  return disabled_by_rule_;
}

AutocorrectManager::PendingAutocorrectState::PendingAutocorrectState(
    const std::u16string& original_text,
    const std::u16string& suggested_text,
    const base::TimeTicks& start_time,
    bool virtual_keyboard_visible)
    : original_text(original_text),
      suggested_text(suggested_text),
      start_time(start_time),
      virtual_keyboard_visible(virtual_keyboard_visible) {}

AutocorrectManager::PendingAutocorrectState::PendingAutocorrectState(
  const PendingAutocorrectState& other) = default;

AutocorrectManager::PendingAutocorrectState::~PendingAutocorrectState() =
    default;

}  // namespace input_method
}  // namespace ash
