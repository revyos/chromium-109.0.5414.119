// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/input_method/autocorrect_manager.h"

#include "ash/constants/ash_features.h"
#include "base/callback_helpers.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ash/input_method/autocorrect_enums.h"
#include "chrome/browser/ash/input_method/suggestion_enums.h"
#include "chrome/browser/ash/input_method/ui/suggestion_details.h"
#include "chrome/browser/ui/ash/keyboard/chrome_keyboard_controller_client.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/testing_profile.h"
#include "components/ukm/test_ukm_recorder.h"
#include "content/public/test/browser_task_environment.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/ime/ash/ime_bridge.h"
#include "ui/base/ime/ash/input_method_ash.h"
#include "ui/base/ime/ash/mock_ime_input_context_handler.h"
#include "ui/base/ime/fake_text_input_client.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/keycodes/dom/dom_code.h"

namespace ash {
namespace input_method {

namespace {

using ::testing::_;
using ::testing::SetArgPointee;
using ::testing::DoAll;
using ::testing::Return;

using UkmEntry = ukm::builders::InputMethod_Assistive_AutocorrectV2;

constexpr char kCoverageHistogramName[] = "InputMethod.Assistive.Coverage";
constexpr char kSuccessHistogramName[] = "InputMethod.Assistive.Success";
constexpr char kDelayHistogramName[] =
    "InputMethod.Assistive.Autocorrect.Delay";
constexpr char kAutocorrectActionHistogramName[] =
    "InputMethod.Assistive.Autocorrect.Actions";
constexpr char kVKAutocorrectActionHistogramName[] =
    "InputMethod.Assistive.Autocorrect.Actions.VK";
constexpr char kVKAutocorrectV2ActionHistogramName[] =
    "InputMethod.Assistive.AutocorrectV2.Actions.VK";
constexpr char kPKAutocorrectV2ActionHistogramName[] =
    "InputMethod.Assistive.AutocorrectV2.Actions.PK";
constexpr char kAutocorrectV2AcceptLatency[] =
    "InputMethod.Assistive.AutocorrectV2.Latency.Accept";
constexpr char kAutocorrectV2RejectLatency[] =
    "InputMethod.Assistive.AutocorrectV2.Latency.Reject";
constexpr char kAutocorrectV2ExitFieldLatency[] =
    "InputMethod.Assistive.AutocorrectV2.Latency.ExitField";
constexpr char kAutocorrectV2VkPendingLatency[] =
    "InputMethod.Assistive.AutocorrectV2.Latency.VkPending";
constexpr char kAutocorrectV2PkPendingLatency[] =
    "InputMethod.Assistive.AutocorrectV2.Latency.PkPending";
constexpr char kAutocorrectV2QualityVkAcceptedHistName[] =
    "InputMethod.Assistive.AutocorrectV2.Quality.VkAccepted";
constexpr char kAutocorrectV2QualityVkRejectedHistName[] =
    "InputMethod.Assistive.AutocorrectV2.Quality.VkRejected";
constexpr char kAutocorrectV2QualityPkAcceptedHistName[] =
    "InputMethod.Assistive.AutocorrectV2.Quality.PkAccepted";
constexpr char kAutocorrectV2QualityPkRejectedHistName[] =
    "InputMethod.Assistive.AutocorrectV2.Quality.PkRejected";
constexpr char kAutocorrectV2Distance2dVkAcceptedHistName[] =
    "InputMethod.Assistive.AutocorrectV2.Distance."
    "OriginalLengthVsLevenshteinDistance.VkAccepted";
constexpr char kAutocorrectV2Distance2dVkRejectedHistName[] =
    "InputMethod.Assistive.AutocorrectV2.Distance."
    "OriginalLengthVsLevenshteinDistance.VkRejected";
constexpr char kAutocorrectV2Distance2dPkAcceptedHistName[] =
    "InputMethod.Assistive.AutocorrectV2.Distance."
    "OriginalLengthVsLevenshteinDistance.PkAccepted";
constexpr char kAutocorrectV2Distance2dPkRejectedHistName[] =
    "InputMethod.Assistive.AutocorrectV2.Distance."
    "OriginalLengthVsLevenshteinDistance.PkRejected";
constexpr char kAutocorrectV2DistanceSuggestedVkAcceptedHistName[] =
    "InputMethod.Assistive.AutocorrectV2.Distance.SuggestedLength.VkAccepted";
constexpr char kAutocorrectV2DistanceSuggestedVkRejectedHistName[] =
    "InputMethod.Assistive.AutocorrectV2.Distance.SuggestedLength.VkRejected";
constexpr char kAutocorrectV2DistanceSuggestedPkAcceptedHistName[] =
    "InputMethod.Assistive.AutocorrectV2.Distance.SuggestedLength.PkAccepted";
constexpr char kAutocorrectV2DistanceSuggestedPkRejectedHistName[] =
    "InputMethod.Assistive.AutocorrectV2.Distance.SuggestedLength.PkRejected";
constexpr char kAutocorrectV2PkUserPreferenceAll[] =
    "InputMethod.Assistive.AutocorrectV2.PkUserPreference.All";
constexpr char kAutocorrectV2PkUserPreferenceEnglish[] =
    "InputMethod.Assistive.AutocorrectV2.PkUserPreference.English";

constexpr char kUsEnglishEngineId[] = "xkb:us::eng";
constexpr char kUsInternationalEngineId[] = "xkb:us:intl:eng";
constexpr char kSpainSpanishEngineId[] = "xkb:es::spa";
constexpr char kLatinAmericaSpanishEngineId[] = "xkb:latam::spa";
constexpr char kBrazilPortugeseEngineId[] = "xkb:br::por";
constexpr char kFranceFrenchEngineId[] = "xkb:fr::fra";

constexpr int kContextId = 5;

// A helper for testing autocorrect histograms. There are redundant metrics
// for each autocorrect action and the helper ensures that all the relevant
// metrics for one action are updated properly.
void ExpectAutocorrectHistograms(const base::HistogramTester& histogram_tester,
                                 bool visible_vk,
                                 int window_shown,
                                 int underlined,
                                 int reverted,
                                 int accepted,
                                 int cleared_underline,
                                 int exited_text_field_with_underline = 0,
                                 int invalid_range = 0) {
  // Window shown metrics.
  histogram_tester.ExpectBucketCount(kCoverageHistogramName,
                                     AssistiveType::kAutocorrectWindowShown,
                                     window_shown);
  histogram_tester.ExpectBucketCount(kAutocorrectActionHistogramName,
                                     AutocorrectActions::kWindowShown,
                                     window_shown);
  if (visible_vk) {
    histogram_tester.ExpectBucketCount(kVKAutocorrectActionHistogramName,
                                       AutocorrectActions::kWindowShown,
                                       window_shown);
    histogram_tester.ExpectBucketCount(kVKAutocorrectV2ActionHistogramName,
                                       AutocorrectActions::kWindowShown,
                                       window_shown);
  } else {
    histogram_tester.ExpectBucketCount(kPKAutocorrectV2ActionHistogramName,
                                       AutocorrectActions::kWindowShown,
                                       window_shown);
  }

  // Underlined metrics.
  histogram_tester.ExpectBucketCount(kCoverageHistogramName,
                                     AssistiveType::kAutocorrectUnderlined,
                                     underlined);
  histogram_tester.ExpectBucketCount(kAutocorrectActionHistogramName,
                                     AutocorrectActions::kUnderlined,
                                     underlined);
  if (visible_vk) {
    histogram_tester.ExpectBucketCount(kVKAutocorrectActionHistogramName,
                                       AutocorrectActions::kUnderlined,
                                       underlined);
    histogram_tester.ExpectBucketCount(kVKAutocorrectV2ActionHistogramName,
                                       AutocorrectActions::kUnderlined,
                                       underlined);
  } else {
    histogram_tester.ExpectBucketCount(kPKAutocorrectV2ActionHistogramName,
                                       AutocorrectActions::kUnderlined,
                                       underlined);
  }

  // Revert metrics.
  histogram_tester.ExpectBucketCount(
      kCoverageHistogramName, AssistiveType::kAutocorrectReverted, reverted);
  histogram_tester.ExpectBucketCount(
      kSuccessHistogramName, AssistiveType::kAutocorrectReverted, reverted);
  histogram_tester.ExpectBucketCount(kAutocorrectActionHistogramName,
                                     AutocorrectActions::kReverted, reverted);
  if (visible_vk) {
    histogram_tester.ExpectBucketCount(kVKAutocorrectActionHistogramName,
                                       AutocorrectActions::kReverted, reverted);
    histogram_tester.ExpectBucketCount(kVKAutocorrectV2ActionHistogramName,
                                       AutocorrectActions::kReverted, reverted);
  } else {
    histogram_tester.ExpectBucketCount(kPKAutocorrectV2ActionHistogramName,
                                       AutocorrectActions::kReverted, reverted);
  }

  // Accept metrics.
  histogram_tester.ExpectBucketCount(
      kAutocorrectActionHistogramName,
      AutocorrectActions::kUserAcceptedAutocorrect, accepted);
  if (visible_vk) {
    histogram_tester.ExpectBucketCount(
        kVKAutocorrectActionHistogramName,
        AutocorrectActions::kUserAcceptedAutocorrect, accepted);
    histogram_tester.ExpectBucketCount(
        kVKAutocorrectV2ActionHistogramName,
        AutocorrectActions::kUserAcceptedAutocorrect, accepted);
  } else {
    histogram_tester.ExpectBucketCount(
        kPKAutocorrectV2ActionHistogramName,
        AutocorrectActions::kUserAcceptedAutocorrect, accepted);
  }

  // Clear underline metrics.
  histogram_tester.ExpectBucketCount(
      kAutocorrectActionHistogramName,
      AutocorrectActions::kUserActionClearedUnderline, cleared_underline);
  if (visible_vk) {
    histogram_tester.ExpectBucketCount(
        kVKAutocorrectActionHistogramName,
        AutocorrectActions::kUserActionClearedUnderline, cleared_underline);
    histogram_tester.ExpectBucketCount(
        kVKAutocorrectV2ActionHistogramName,
        AutocorrectActions::kUserActionClearedUnderline, cleared_underline);
  } else {
    histogram_tester.ExpectBucketCount(
        kPKAutocorrectV2ActionHistogramName,
        AutocorrectActions::kUserActionClearedUnderline, cleared_underline);
  }

  // Invalid Range metrics.
  histogram_tester.ExpectBucketCount(kAutocorrectActionHistogramName,
                                     AutocorrectActions::kInvalidRange,
                                     invalid_range);
  if (visible_vk) {
    histogram_tester.ExpectBucketCount(kVKAutocorrectActionHistogramName,
                                       AutocorrectActions::kInvalidRange,
                                       invalid_range);
    histogram_tester.ExpectBucketCount(kVKAutocorrectV2ActionHistogramName,
                                       AutocorrectActions::kInvalidRange,
                                       invalid_range);
  } else {
    histogram_tester.ExpectBucketCount(kPKAutocorrectV2ActionHistogramName,
                                       AutocorrectActions::kInvalidRange,
                                       invalid_range);
  }

  // Exited text field with underline.
  histogram_tester.ExpectBucketCount(
      kAutocorrectActionHistogramName,
      AutocorrectActions::kUserExitedTextFieldWithUnderline,
      exited_text_field_with_underline);
  if (visible_vk) {
    histogram_tester.ExpectBucketCount(
        kVKAutocorrectActionHistogramName,
        AutocorrectActions::kUserExitedTextFieldWithUnderline,
        exited_text_field_with_underline);
    histogram_tester.ExpectBucketCount(
        kVKAutocorrectV2ActionHistogramName,
        AutocorrectActions::kUserExitedTextFieldWithUnderline,
        exited_text_field_with_underline);
  } else {
    histogram_tester.ExpectBucketCount(
        kPKAutocorrectV2ActionHistogramName,
        AutocorrectActions::kUserExitedTextFieldWithUnderline,
        exited_text_field_with_underline);
  }

  const int total_actions = window_shown + underlined + reverted + accepted +
                            cleared_underline +
                            exited_text_field_with_underline + invalid_range;
  const int total_coverage = window_shown + underlined + reverted;

  // Count total bucket to test side-effects and make the helper robust against
  // future changes of the metric buckets.
  histogram_tester.ExpectTotalCount(kCoverageHistogramName, total_coverage);
  histogram_tester.ExpectTotalCount(kSuccessHistogramName, reverted);
  histogram_tester.ExpectTotalCount(kAutocorrectActionHistogramName,
                                    total_actions);
  histogram_tester.ExpectTotalCount(kVKAutocorrectActionHistogramName,
                                    visible_vk ? total_actions : 0);
  histogram_tester.ExpectTotalCount(kVKAutocorrectV2ActionHistogramName,
                                    visible_vk ? total_actions : 0);
  histogram_tester.ExpectTotalCount(kPKAutocorrectV2ActionHistogramName,
                                    visible_vk ? 0 : total_actions);

  // Latency metrics.
  histogram_tester.ExpectTotalCount(kDelayHistogramName, reverted);
  histogram_tester.ExpectTotalCount(kAutocorrectV2AcceptLatency, accepted);
  histogram_tester.ExpectTotalCount(kAutocorrectV2ExitFieldLatency,
                                    exited_text_field_with_underline);
  histogram_tester.ExpectTotalCount(
      kAutocorrectV2RejectLatency,
      reverted + cleared_underline + invalid_range);
  histogram_tester.ExpectTotalCount(
      kAutocorrectV2VkPendingLatency,
      visible_vk ? cleared_underline + reverted + accepted + invalid_range +
                       exited_text_field_with_underline
                 : 0);
  histogram_tester.ExpectTotalCount(
      kAutocorrectV2PkPendingLatency,
      visible_vk ? 0
                 : cleared_underline + reverted + accepted + invalid_range +
                       exited_text_field_with_underline);
}

// A helper to create properties for hidden undo window.
AssistiveWindowProperties CreateHiddenUndoWindowProperties() {
  AssistiveWindowProperties window_properties;
  window_properties.type = ash::ime::AssistiveWindowType::kUndoWindow;
  window_properties.visible = false;
  return window_properties;
}

// A helper to create properties for shown undo window.
AssistiveWindowProperties CreateVisibleUndoWindowProperties(
    const std::u16string& original_text,
    const std::u16string& autocorrected_text) {
  AssistiveWindowProperties window_properties;
  window_properties.type = ash::ime::AssistiveWindowType::kUndoWindow;
  window_properties.visible = true;
  window_properties.announce_string =
      l10n_util::GetStringFUTF16(IDS_SUGGESTION_AUTOCORRECT_UNDO_WINDOW_SHOWN,
                                 original_text, autocorrected_text);
  return window_properties;
}

// A helper to create highlighted undo button in assistive window.
ui::ime::AssistiveWindowButton CreateHighlightedUndoButton(
    const std::u16string& original_text) {
  ui::ime::AssistiveWindowButton button = ui::ime::AssistiveWindowButton();
  button.id = ui::ime::ButtonId::kUndo;
  button.window_type = ash::ime::AssistiveWindowType::kUndoWindow;
  button.announce_string = l10n_util::GetStringFUTF16(
      IDS_SUGGESTION_AUTOCORRECT_UNDO_BUTTON, original_text);
  return button;
}

// A helper for creating key event.
ui::KeyEvent CreateKeyEvent(ui::DomKey key, ui::DomCode code) {
  return ui::KeyEvent(ui::ET_KEY_PRESSED, ui::VKEY_UNKNOWN, code, ui::EF_NONE,
                      key, ui::EventTimeForNow());
}

ui::KeyEvent KeyA() {
  return CreateKeyEvent(ui::DomKey::FromCharacter('a'), ui::DomCode::US_A);
}

void SetAutocorrectPreferenceTo(Profile& profile,
                                const std::string& engine_id,
                                bool autocorrect_enabled) {
  base::Value input_method_setting(base::Value::Type::DICTIONARY);
  input_method_setting.SetPath(
      engine_id + ".physicalKeyboardAutoCorrectionLevel",
      base::Value(autocorrect_enabled ? 1 : 0));
  profile.GetPrefs()->Set(::prefs::kLanguageInputMethodSpecificSettings,
                          input_method_setting);
}

class MockSuggestionHandler : public SuggestionHandlerInterface {
 public:
  MOCK_METHOD(bool,
              DismissSuggestion,
              (int context_id, std::string* error),
              (override));
  MOCK_METHOD(bool,
              SetSuggestion,
              (int context_id,
               const ui::ime::SuggestionDetails& details,
               std::string* error),
              (override));
  MOCK_METHOD(bool,
              AcceptSuggestion,
              (int context_id, std::string* error),
              (override));
  MOCK_METHOD(void,
              OnSuggestionsChanged,
              (const std::vector<std::string>& suggestions),
              (override));
  MOCK_METHOD(bool,
              SetButtonHighlighted,
              (int context_id,
               const ui::ime::AssistiveWindowButton& button,
               bool highlighted,
               std::string* error),
              (override));
  MOCK_METHOD(void,
              ClickButton,
              (const ui::ime::AssistiveWindowButton& button),
              (override));
  MOCK_METHOD(bool,
              AcceptSuggestionCandidate,
              (int context_id,
               const std::u16string& candidate,
               size_t delete_previous_utf16_len,
               std::string* error),
              (override));
  MOCK_METHOD(bool,
              SetAssistiveWindowProperties,
              (int context_id,
               const AssistiveWindowProperties& assistive_window,
               std::string* error),
              (override));
  MOCK_METHOD(void, Announce, (const std::u16string& text), (override));
};

class AutocorrectManagerTest : public testing::Test {
 protected:
  AutocorrectManagerTest()
      : profile_(std::make_unique<TestingProfile>()),
        manager_(&mock_suggestion_handler_, profile_.get()) {
    // Disable ImeRulesConfigs by default.
    feature_list_.InitWithFeatures({}, {ash::features::kImeRuleConfig});
    ui::IMEBridge::Get()->SetInputContextHandler(
        &mock_ime_input_context_handler_);
    keyboard_client_ = ChromeKeyboardControllerClient::CreateForTest();
    keyboard_client_->set_keyboard_visible_for_test(false);
  }

  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  ::base::test::ScopedFeatureList feature_list_;
  ui::MockIMEInputContextHandler mock_ime_input_context_handler_;
  ::testing::StrictMock<MockSuggestionHandler> mock_suggestion_handler_;
  std::unique_ptr<Profile> profile_;
  std::unique_ptr<ChromeKeyboardControllerClient> keyboard_client_;
  AutocorrectManager manager_;
  base::HistogramTester histogram_tester_;
};

TEST_F(AutocorrectManagerTest,
       HandleAutocorrectSetsRangeWhenNoPendingAutocorrectExists) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(0, 3));
}

TEST_F(AutocorrectManagerTest,
       HandleAutocorrectSetsRangeWhenPendingAutocorrectExists) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.HandleAutocorrect(gfx::Range(4, 7), u"cn", u"can");
  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(4, 7));
}

TEST_F(AutocorrectManagerTest,
       HandleAutocorrectDoesNotSetRangeWhenInputContextIsNull) {
  ui::IMEBridge::Get()->SetInputContextHandler(nullptr);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"cn", u"can");
  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range());
}

TEST_F(AutocorrectManagerTest,
       HandleAutocorrectClearsRangeWithEmptyInputRange) {
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(3, 7),
                                                      base::DoNothing());
  manager_.HandleAutocorrect(gfx::Range(), u"", u"");
  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range());
}

TEST_F(AutocorrectManagerTest, OnKeyEventDoesNotClearAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  const ui::KeyEvent key_event =
      CreateKeyEvent(ui::DomKey::FromCharacter('a'), ui::DomCode::US_A);
  EXPECT_FALSE(manager_.OnKeyEvent(key_event));
  EXPECT_FALSE(manager_.OnKeyEvent(key_event));
  EXPECT_FALSE(manager_.OnKeyEvent(key_event));
  EXPECT_FALSE(manager_.OnKeyEvent(key_event));
  EXPECT_FALSE(manager_.OnKeyEvent(key_event));

  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(0, 3));
}

TEST_F(AutocorrectManagerTest,
       TypingFewCharsAfterRangeDoesNotClearAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  manager_.OnSurroundingTextChanged(u"the a", 5, 5);
  manager_.OnSurroundingTextChanged(u"the ab", 6, 6);

  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(0, 3));
}

TEST_F(AutocorrectManagerTest,
       TypingEnoughCharsAfterRangeClearsAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  manager_.OnSurroundingTextChanged(u"the a", 5, 5);
  manager_.OnSurroundingTextChanged(u"the ab", 6, 6);
  manager_.OnSurroundingTextChanged(u"the abc", 7, 7);

  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range());
}

TEST_F(AutocorrectManagerTest,
       TypingFewCharsBeforeRangeDoesNotClearAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(1, 4), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u" the ", 5, 5);

  // Move cursor to position 0.
  manager_.OnSurroundingTextChanged(u" the ", 0, 0);
  // Add two chars and move the ranges accordingly.
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(2, 5),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"a the ", 1, 1);
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(3, 6),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"ab the ", 2, 2);

  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(3, 6));
}

TEST_F(AutocorrectManagerTest,
       TypingEnoughCharsBeforeRangeClearsAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(1, 4), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u" the ", 5, 5);

  // Move cursor to position 0.
  manager_.OnSurroundingTextChanged(u" the ", 0, 0);
  // Add three chars and move the range accordingly.
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(2, 5),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"a the ", 1, 1);
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(3, 6),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"ab the ", 2, 2);
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(4, 7),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"abc the ", 3, 3);

  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range());
}

TEST_F(AutocorrectManagerTest,
       TypingFewCharsBeforeAndAfterRangeDoesNotClearAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(1, 4), u"teh", u"the");

  manager_.OnSurroundingTextChanged(u" the ", 5, 5);
  manager_.OnSurroundingTextChanged(u" the a", 6, 6);
  manager_.OnSurroundingTextChanged(u" the a", 0, 0);
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(2, 5),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"b the a", 1, 1);

  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(2, 5));
}

TEST_F(AutocorrectManagerTest,
       TypingEnoughCharsAfterAndBeforeRangeClearsAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(1, 4), u"teh", u"the");

  manager_.OnSurroundingTextChanged(u" the ", 5, 5);
  manager_.OnSurroundingTextChanged(u" the a", 6, 6);
  manager_.OnSurroundingTextChanged(u" the a", 0, 0);
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(2, 5),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"b the a", 1, 1);
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(3, 6),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"bc the a", 2, 2);

  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range());
}

TEST_F(AutocorrectManagerTest,
       RemovingCharsCloseToRangeDoesNotClearAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Add characters.
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  manager_.OnSurroundingTextChanged(u"the ab", 6, 6);
  // Now remove them.
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(0, 3));
}

TEST_F(AutocorrectManagerTest,
       PastingEnoughCharsAndRemovingFewStillClearsAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Add characters.
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  manager_.OnSurroundingTextChanged(u"the ab", 6, 6);
  // Now removing them should not be counted.
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  // Now addition of a new character must trigger the clearance process,
  // to ensure backspaced does not impact the output.
  manager_.OnSurroundingTextChanged(u"the a", 5, 5);

  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range());
}

TEST_F(AutocorrectManagerTest,
       PastingFewCharsBeforeRangeDoesNotClearAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(1, 4), u"teh", u"the");

  manager_.OnSurroundingTextChanged(u" the ", 5, 5);
  manager_.OnSurroundingTextChanged(u" the ", 0, 0);
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(3, 6),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"ab the ", 2, 2);

  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(3, 6));
}

TEST_F(AutocorrectManagerTest,
       PastingEnoughCharsBeforeRangeClearsAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(1, 4), u"teh", u"the");

  manager_.OnSurroundingTextChanged(u" the ", 5, 5);
  manager_.OnSurroundingTextChanged(u" the ", 0, 0);
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(4, 7),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"abc the ", 3, 3);

  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range());
}

TEST_F(AutocorrectManagerTest,
       OnBlurClearsAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(1, 4), u"teh", u"the");
  manager_.OnBlur();

  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range());
}

TEST_F(AutocorrectManagerTest,
       OnFocusClearsAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(1, 4), u"teh", u"the");
  manager_.OnFocus(1);

  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range());
}

TEST_F(AutocorrectManagerTest, MovingCursorInsideRangeShowsAssistiveWindow) {
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/4,
                                    /*anchor_pos=*/4);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  AssistiveWindowProperties properties =
      CreateVisibleUndoWindowProperties(u"teh", u"the");
  EXPECT_CALL(mock_suggestion_handler_,
              SetAssistiveWindowProperties(_, properties, _));

  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/1,
                                    /*anchor_pos=*/1);
}

TEST_F(AutocorrectManagerTest,
       MovingCursorInsideRangeDoesNotShowUndoWindowWhenRangeNotValidated) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Range is not validate validated yet. So, no expectation on show undo
  // window call. If it happens, test will fail by StrictMock.
  manager_.OnSurroundingTextChanged(u"teh ", /*cursor_pos=*/1,
                                    /*anchor_pos=*/1);
}

TEST_F(AutocorrectManagerTest, MovingCursorOutsideRangeHidesAssistiveWindow) {
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/4,
                                    /*anchor_pos=*/4);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  {
    ::testing::InSequence seq;

    AssistiveWindowProperties shown_properties =
        CreateVisibleUndoWindowProperties(u"teh", u"the");
    EXPECT_CALL(mock_suggestion_handler_,
                SetAssistiveWindowProperties(_, shown_properties, _));

    AssistiveWindowProperties hidden_properties =
        CreateHiddenUndoWindowProperties();
    EXPECT_CALL(mock_suggestion_handler_,
                SetAssistiveWindowProperties(_, hidden_properties, _));
  }

  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/1,
                                    /*anchor_pos=*/1);
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/4,
                                    /*anchor_pos=*/4);
}

TEST_F(AutocorrectManagerTest,
       MovingCursorRetriesPrevFailedUndoWindowHide) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/4,
                                    /*anchor_pos=*/4);

  // Show undo window.
  AssistiveWindowProperties shown_properties =
      CreateVisibleUndoWindowProperties(u"teh", u"the");
  EXPECT_CALL(mock_suggestion_handler_,
              SetAssistiveWindowProperties(_, shown_properties, _));
  manager_.OnSurroundingTextChanged(u"the ", 1, 1);

  // Accept autocorrect implicitly and make the request to hide the window
  // fail.
  AssistiveWindowProperties hidden_properties =
      CreateHiddenUndoWindowProperties();
  EXPECT_CALL(mock_suggestion_handler_,
              SetAssistiveWindowProperties(_, hidden_properties, _))
      .WillOnce(DoAll(SetArgPointee<2>("Error"), Return(false)))
      .RetiresOnSaturation();
  manager_.OnSurroundingTextChanged(u"the abcd", 8, 8);

  // Now moving cursor should retry hiding autocorrect range.
  EXPECT_CALL(mock_suggestion_handler_,
              SetAssistiveWindowProperties(_, hidden_properties, _));
  manager_.OnSurroundingTextChanged(u"the abcd", 7, 7);
}

TEST_F(AutocorrectManagerTest,
       MovingCursorInsideRangeRetriesPrevFailedUndoWindowHide) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/4,
                                    /*anchor_pos=*/4);

  // Show undo window.
  AssistiveWindowProperties shown_properties =
      CreateVisibleUndoWindowProperties(u"teh", u"the");
  EXPECT_CALL(mock_suggestion_handler_,
              SetAssistiveWindowProperties(_, shown_properties, _));
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/1,
                                    /*anchor_pos=*/1);

  // Make first try to hide the window fail.
  AssistiveWindowProperties hidden_properties =
      CreateHiddenUndoWindowProperties();
  EXPECT_CALL(mock_suggestion_handler_,
              SetAssistiveWindowProperties(_, hidden_properties, _))
      .WillOnce(DoAll(SetArgPointee<2>("Error"), Return(false)));
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/4,
                                    /*anchor_pos=*/4);

  {
    ::testing::InSequence seq;

    // Retries previously failed undo window hiding which now
    // succeed.
    EXPECT_CALL(mock_suggestion_handler_,
                SetAssistiveWindowProperties(_, hidden_properties, _));

    // Showing new undo window.
    EXPECT_CALL(mock_suggestion_handler_,
                SetAssistiveWindowProperties(_, shown_properties, _));
  }

  // Try hiding undo window before showing it again.
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/1,
                                    /*anchor_pos=*/1);
}

TEST_F(AutocorrectManagerTest,
       ShowingNewUndoWindowStopsRetryingPrevFailedUndoWindowHide) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/4,
                                    /*anchor_pos=*/4);

  // Show the undo window first time.
  AssistiveWindowProperties shown_properties =
      CreateVisibleUndoWindowProperties(u"teh", u"the");
  EXPECT_CALL(mock_suggestion_handler_,
              SetAssistiveWindowProperties(_, shown_properties, _));
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/1,
                                    /*anchor_pos=*/1);

  // Make first two call to hide undo window to fail.
  AssistiveWindowProperties hidden_properties =
      CreateHiddenUndoWindowProperties();
  EXPECT_CALL(mock_suggestion_handler_,
              SetAssistiveWindowProperties(_, hidden_properties, _))
      .Times(2)
      .WillRepeatedly(DoAll(SetArgPointee<2>("Error"), Return(false)))
      .RetiresOnSaturation();

  // Handle a new range to allow triggering an undo window override.
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Show a new undo window.
  EXPECT_CALL(mock_suggestion_handler_,
              SetAssistiveWindowProperties(_, shown_properties, _));
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/1,
                                    /*anchor_pos=*/1);

  // No retry should be applied to hide undo window as it is overridden.
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/2,
                                    /*anchor_pos=*/2);
}

TEST_F(AutocorrectManagerTest, FocusChangeHidesUndoWindow) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/4,
                                    /*anchor_pos=*/4);

  // Show a window.
  AssistiveWindowProperties shown_properties =
      CreateVisibleUndoWindowProperties(u"teh", u"the");
  EXPECT_CALL(mock_suggestion_handler_,
              SetAssistiveWindowProperties(_, shown_properties, _));
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/1,
                                    /*anchor_pos=*/1);

  // OnFocus should try hiding the window.
  AssistiveWindowProperties hidden_properties =
      CreateHiddenUndoWindowProperties();
  EXPECT_CALL(mock_suggestion_handler_,
              SetAssistiveWindowProperties(_, hidden_properties, _));

  manager_.OnFocus(1);
}

TEST_F(AutocorrectManagerTest, OnFocusRetriesHidingUndoWindow) {
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/4,
                                    /*anchor_pos=*/4);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Show undo window.
  AssistiveWindowProperties shown_properties =
      CreateVisibleUndoWindowProperties(u"teh", u"the");
  EXPECT_CALL(mock_suggestion_handler_,
              SetAssistiveWindowProperties(_, shown_properties, _));
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/1,
                                    /*anchor_pos=*/1);

  // Make it fail to hide window for OnBlur.
  AssistiveWindowProperties hidden_properties =
      CreateHiddenUndoWindowProperties();
  EXPECT_CALL(mock_suggestion_handler_,
              SetAssistiveWindowProperties(_, hidden_properties, _))
      .WillOnce(DoAll(SetArgPointee<2>("Error"), Return(false)));
  manager_.OnBlur();

  // OnFocus must try to hide undo window.
  EXPECT_CALL(mock_suggestion_handler_,
              SetAssistiveWindowProperties(_, hidden_properties, _));
  manager_.OnFocus(1);
}

TEST_F(AutocorrectManagerTest,
       PressingUpArrowKeyHighlightsUndoButtonWhenUndoWindowIsVisible) {
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/4,
                                    /*anchor_pos=*/4);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  {
    ::testing::InSequence seq;

    AssistiveWindowProperties shown_properties =
        CreateVisibleUndoWindowProperties(u"teh", u"the");

    EXPECT_CALL(mock_suggestion_handler_,
                SetAssistiveWindowProperties(_, shown_properties, _));

    ui::ime::AssistiveWindowButton button =
          CreateHighlightedUndoButton(u"teh");
    EXPECT_CALL(mock_suggestion_handler_,
                SetButtonHighlighted(_, button, true, _));
  }

  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/1,
                                    /*anchor_pos=*/1);
  manager_.OnKeyEvent(CreateKeyEvent(ui::DomKey::NONE, ui::DomCode::ARROW_UP));
}

TEST_F(AutocorrectManagerTest,
       PressingTabKeyHighlightsUndoButtonWhenUndoWindowIsVisible) {
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/4,
                                    /*anchor_pos=*/4);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  {
    ::testing::InSequence seq;

    AssistiveWindowProperties shown_properties =
        CreateVisibleUndoWindowProperties(u"teh", u"the");

    EXPECT_CALL(mock_suggestion_handler_,
                SetAssistiveWindowProperties(_, shown_properties, _));

    ui::ime::AssistiveWindowButton button = CreateHighlightedUndoButton(u"teh");
    EXPECT_CALL(mock_suggestion_handler_,
                SetButtonHighlighted(_, button, true, _));
  }

  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/1,
                                    /*anchor_pos=*/1);
  manager_.OnKeyEvent(CreateKeyEvent(ui::DomKey::NONE, ui::DomCode::TAB));
}

TEST_F(AutocorrectManagerTest,
       PressingEnterKeyHidesUndoWindowWhenButtonIsHighlighted) {
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/4,
                                    /*anchor_pos=*/4);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  {
    ::testing::InSequence seq;

    AssistiveWindowProperties shown_properties =
        CreateVisibleUndoWindowProperties(u"teh", u"the");

    EXPECT_CALL(mock_suggestion_handler_,
                SetAssistiveWindowProperties(_, shown_properties, _));

    ui::ime::AssistiveWindowButton button =
        CreateHighlightedUndoButton(u"teh");
    EXPECT_CALL(mock_suggestion_handler_,
                SetButtonHighlighted(_, button, true, _));

    AssistiveWindowProperties hidden_properties =
        CreateHiddenUndoWindowProperties();
    EXPECT_CALL(mock_suggestion_handler_,
                SetAssistiveWindowProperties(_, hidden_properties, _));
  }

  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/1,
                                    /*anchor_pos=*/1);
  manager_.OnKeyEvent(CreateKeyEvent(ui::DomKey::NONE, ui::DomCode::ARROW_UP));
  manager_.OnKeyEvent(CreateKeyEvent(ui::DomKey::NONE, ui::DomCode::ENTER));
}

TEST_F(AutocorrectManagerTest, UndoAutocorrectSingleWordInComposition) {
  ui::FakeTextInputClient fake_text_input_client(ui::TEXT_INPUT_TYPE_TEXT);
  ui::InputMethodAsh ime(nullptr);
  ui::IMEBridge::Get()->SetInputContextHandler(&ime);
  ime.SetFocusedTextInputClient(&fake_text_input_client);

  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", /*cursor_pos=*/4,
                                    /*anchor_pos=*/4);

  // Move cursor to the middle of 'the' and bring the text into composition.
  fake_text_input_client.SetTextAndSelection(u"the ", gfx::Range(2));
  ime.SetComposingRange(0, 3, {});

  manager_.UndoAutocorrect();

  EXPECT_EQ(fake_text_input_client.text(), u"teh ");
}

TEST_F(AutocorrectManagerTest, UndoAutocorrectDoesNotApplyOnRangeNotValidated) {
  ui::FakeTextInputClient fake_text_input_client(ui::TEXT_INPUT_TYPE_TEXT);
  ui::InputMethodAsh ime(nullptr);
  ui::IMEBridge::Get()->SetInputContextHandler(&ime);
  ime.SetFocusedTextInputClient(&fake_text_input_client);

  // No OnSurroundingTextChanged is called to validate the suggestion.
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Move cursor to the middle of 'the' and bring the text into composition.
  fake_text_input_client.SetTextAndSelection(u"the ", gfx::Range(2));
  ime.SetComposingRange(0, 3, {});

  manager_.UndoAutocorrect();

  // Undo is not applied.
  EXPECT_EQ(fake_text_input_client.text(), u"the ");
}

TEST_F(AutocorrectManagerTest, UndoAutocorrectMultipleWordInComposition) {
  ui::FakeTextInputClient fake_text_input_client(ui::TEXT_INPUT_TYPE_TEXT);
  ui::InputMethodAsh ime(nullptr);
  ui::IMEBridge::Get()->SetInputContextHandler(&ime);
  ime.SetFocusedTextInputClient(&fake_text_input_client);

  manager_.HandleAutocorrect(gfx::Range(0, 11), u"helloworld", u"hello world");

  manager_.OnSurroundingTextChanged(u"hello world ", /*cursor_pos=*/12,
                                    /*anchor_pos=*/12);

  // Move cursor to the middle of 'hello' and bring the word into composition.
  fake_text_input_client.SetTextAndSelection(u"hello world ", gfx::Range(2));
  ime.SetComposingRange(0, 5, {});

  manager_.UndoAutocorrect();

  EXPECT_EQ(fake_text_input_client.text(), u"helloworld ");
}

TEST_F(AutocorrectManagerTest, MovingCursorDoesNotAcceptAutocorrect) {
  manager_.HandleAutocorrect(gfx::Range(5, 8), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"abcd the efghij", 4, 4);

  // Move cursor to different positions in one session does not
  // accept or clear the the autocorrect range implicitly.
  manager_.OnSurroundingTextChanged(u"abcd the efghij", 15, 15);
  manager_.OnSurroundingTextChanged(u"abcd the efghij", 0, 0);
  manager_.OnSurroundingTextChanged(u"abcd the efghij", 4, 4);
  manager_.OnSurroundingTextChanged(u"abcd the efghij", 9, 9);

  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(5, 8));
}

TEST_F(AutocorrectManagerTest,
       InsertingFewCharsDoesNotRecordMetricsForPendingAutocorrect) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  // Add characters.
  manager_.OnSurroundingTextChanged(u" the b", 6, 6);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       InsertingEnoughCharsRecordsMetricWhenAcceptingAutocorrect) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Add characters.
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  manager_.OnSurroundingTextChanged(u"c the b", 7, 7);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/1,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       RemovingCharsDoesNotRecordMetricsForPendingAutocorrect) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Add characters.
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  manager_.OnSurroundingTextChanged(u"the ab", 6, 6);
  // Now remove them.
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       InsertingCharsRecordsMetricsWhenClearingAutocorrect) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  manager_.OnSurroundingTextChanged(u"the a", 5, 5);

  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u" the b", 6, 6);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/1);
}

TEST_F(AutocorrectManagerTest,
       InsertingCharsDoesNotRecordsMetricsWhenSetRangeFails) {
  // Disable autocorrect.
  mock_ime_input_context_handler_.set_autocorrect_enabled(false);

  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  manager_.OnSurroundingTextChanged(u"the a", 5, 5);

  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u" the b", 6, 6);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/0,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       OnSurroundingCallDoesNotRecordMetricsWhenClearingInvalidRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Range not validated yet.
  manager_.OnSurroundingTextChanged(u"t ", 2, 2);

  // Clear range.
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(),
                                                      base::DoNothing());

  // Empty range is received and ignored because the new suggestion is still
  // not validated.
  manager_.OnSurroundingTextChanged(u"th ", 3, 3);

  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       OnSurroundingCallRecordsMetricsWhenClearingValidatedRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Validate the range.
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  // Clear the range.
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(),
                                                      base::DoNothing());
  // Process the cleared range ('the' is mutated to implicitly reject it).
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);

  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/1);
}

TEST_F(AutocorrectManagerTest,
       OnSurroundingRecordsMetricsCorrectlyForNullInputContext) {
  // Create a pending autocorrect range.
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Make Input context null.
  ui::IMEBridge::Get()->SetInputContextHandler(nullptr);
  // Null input context invalidates the previous range even if rules are
  // triggered to accept the range.
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  manager_.OnSurroundingTextChanged(u"the abc", 7, 7);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline*/ 0,
                              /*invalid_range*/ 1);
}

TEST_F(AutocorrectManagerTest,
       MovingCursorDoesNotRecordMetricsForPendingAutocorrect) {
  manager_.HandleAutocorrect(gfx::Range(4, 7), u"teh", u"the");

  manager_.OnSurroundingTextChanged(u"abc the def", 8, 8);
  manager_.OnSurroundingTextChanged(u"abc the def", 1, 1);
  manager_.OnSurroundingTextChanged(u"abc the def", 10, 10);
  manager_.OnSurroundingTextChanged(u"abc the def", 3, 3);
  manager_.OnSurroundingTextChanged(u"abc the def", 8, 8);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       MovingCursorToRangeStartRecordsMetricsForShownUndoWindow) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // This suppresses strict mock.
  EXPECT_CALL(mock_suggestion_handler_, SetAssistiveWindowProperties(_, _, _));

  manager_.OnSurroundingTextChanged(u"the", 0, 0);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/1, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       MovingCursorToRangeEndRecordsMetricsForShownUndoWindow) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // This suppresses strict mock.
  EXPECT_CALL(mock_suggestion_handler_, SetAssistiveWindowProperties(_, _, _));

  // Moving cursor inside the range does not increase window_shown.
  manager_.OnSurroundingTextChanged(u"the", 3, 3);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/1, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       KeepingCursorInsideRangeRecordsMetricsForShownUndoWindowOnce) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // This suppresses strict mock.
  EXPECT_CALL(mock_suggestion_handler_, SetAssistiveWindowProperties(_, _, _));

  manager_.OnSurroundingTextChanged(u"the", 0, 0);
  manager_.OnSurroundingTextChanged(u"the", 3, 3);
  manager_.OnSurroundingTextChanged(u"the", 2, 2);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/1, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       MovingCursorOutThenInsideRangeDoesNotRecordsMetricsTwice) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // The function is called two times for show and one time for hide.
  // This suppresses strict mock.
  EXPECT_CALL(mock_suggestion_handler_, SetAssistiveWindowProperties(_, _, _))
      .Times(3);

  // Moving cursor first inside range, then outside the range and then again
  // back to the range increments the metric for shown window twice.
  manager_.OnSurroundingTextChanged(u"the", 1, 1);
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  manager_.OnSurroundingTextChanged(u"the", 3, 3);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/1, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       OnKeyEventDoesNotRecordMetricsForAcceptingOrClearingAutocorrect) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  const ui::KeyEvent key_event =
      CreateKeyEvent(ui::DomKey::FromCharacter('a'), ui::DomCode::US_A);
  manager_.OnKeyEvent(key_event);
  manager_.OnKeyEvent(key_event);
  manager_.OnKeyEvent(key_event);
  manager_.OnKeyEvent(key_event);
  manager_.OnKeyEvent(key_event);

  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       OnKeyEventDoesNotRecordMetricsAfterClearingRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(),
                                                      base::DoNothing());
  const ui::KeyEvent key_event =
      CreateKeyEvent(ui::DomKey::FromCharacter('a'), ui::DomCode::US_A);
  manager_.OnKeyEvent(key_event);
  manager_.OnKeyEvent(key_event);
  manager_.OnKeyEvent(key_event);
  manager_.OnKeyEvent(key_event);
  manager_.OnKeyEvent(key_event);

  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest, OnBlurRecordsMetricsWhenClearingRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnBlur();
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/1);
}

TEST_F(AutocorrectManagerTest,
       OnBlurDoesNoRecordMetricsWhenNoPendingAutocorrectExists) {
  manager_.OnBlur();
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/0,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       OnBlurDoesNoRecordMetricsWhenInputContextIsNull) {
  // Make Input context null.
  ui::IMEBridge::Get()->SetInputContextHandler(nullptr);
  manager_.OnBlur();
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/0,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/0);
}

TEST_F(AutocorrectManagerTest, OnFocusRecordsMetricsWhenClearingRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnFocus(1);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/1);
}

TEST_F(AutocorrectManagerTest,
       OnFocusDoesNoRecordMetricsWhenNoPendingAutocorrectExists) {
  manager_.OnFocus(1);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/0,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       HandleAutocorrectRecordsMetricsWhenNoPendingAutocorrectExists) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       HandleAutocorrectDoesNotRecordMetricsWhenSetRangeFails) {
  // Disable autocorrect.
  mock_ime_input_context_handler_.set_autocorrect_enabled(false);

  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/0,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       HandleAutocorrectRecordsMetricsWhenAcceptingPendingAutocorrect) {
  // Create a pending autocorrect range.
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  // Create a new autocorrect range.
  manager_.HandleAutocorrect(gfx::Range(4, 7), u"cn", u"can");

  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/2,
                              /*reverted=*/0, /*accepted=*/1,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       HandleAutocorrectRecordsMetricsWithPendingRangeAndFailedSetRange) {
  // Enable Autocorrect.
  mock_ime_input_context_handler_.set_autocorrect_enabled(true);

  // Create a pending autocorrect range.
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  // Disable autocorrect.
  mock_ime_input_context_handler_.set_autocorrect_enabled(false);

  // Create a new autocorrect range.
  manager_.HandleAutocorrect(gfx::Range(4, 7), u"cn", u"can");

  // This case should not happen in practice, but the expected result
  // is counting the first autocorrect as rejected given there is no way
  // to know if it was accepted.
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/1);
}

TEST_F(AutocorrectManagerTest,
       HandleAutocorrectRecordsMetricsWhenClearingPendingAutocorrect) {
  // Create a pending autocorrect range.
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  // Clear the previous autocorrect range.
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(),
                                                      base::DoNothing());

  // Handle a new range.
  manager_.HandleAutocorrect(gfx::Range(4, 7), u"cn", u"can");

  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/2,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/1);
}

TEST_F(AutocorrectManagerTest,
       HandleAutocorrectRecordsMetricsCorrectlyForNullInputContext) {
  // Create a pending autocorrect range.
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  // Make Input context null.
  ui::IMEBridge::Get()->SetInputContextHandler(nullptr);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // The pending range must be counted as invalid, but `underlined` metric must
  // not be incremented with the empty input context.
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline*/ 0,
                              /*invalid_range*/ 1);

  // When there is no pending autocorrect range, nothing is incremented.
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline*/ 0,
                              /*invalid_range*/ 1);
}

TEST_F(AutocorrectManagerTest,
       HandleAutocorrectRecordsMetricsForEmptyInputAndNoPendingRange) {
  // Empty input range does not change autocorrect range.
  manager_.HandleAutocorrect(gfx::Range(), u"", u"");
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/0,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       HandleAutocorrectRecordsMetricsForEmptyInputAndPendingRange) {
  // When there is a pending autocorrect, empty input range makes the pending
  // to be counted as accepted.
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  manager_.HandleAutocorrect(gfx::Range(), u"", u"");
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/1,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       HandleAutocorrectRecordsMetricsForEmptyInputAndClearedPending) {
  // When there is a pending autocorrect, but cleared beforehand,
  // empty input range makes the pending to be counted as cleared.
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(),
                                                      base::DoNothing());
  manager_.HandleAutocorrect(gfx::Range(), u"", u"");
  manager_.OnSurroundingTextChanged(u"", 0, 0);

  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/1);
}

TEST_F(AutocorrectManagerTest,
       InsertingCharsDoesNotRecordMetricsForStaleAndAcceptedRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  manager_.OnSurroundingTextChanged(u"the abc", 7, 7);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/1,
                              /*cleared_underline=*/0);

  // Set stale autocorrect range.
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(0, 3),
                                                      base::DoNothing());

  // Adding extra character should not double count.
  manager_.OnSurroundingTextChanged(u"the abcd", 8, 8);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/1,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       InsertingCharsDoesNotRecordMetricsForStaleAndClearedRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/1);

  // Set stale cleared range.
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(0, 3),
                                                      base::DoNothing());

  manager_.OnSurroundingTextChanged(u"the abc", 7, 7);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/1);
}

TEST_F(AutocorrectManagerTest,
       AutocorrectHandlerDoesNotRecordMetricsForStaleAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  manager_.OnSurroundingTextChanged(u"the abc", 7, 7);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/1,
                              /*cleared_underline=*/0);

  // Set stale autocorrect range.
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(0, 3),
                                                      base::DoNothing());

  // Handle a new autocorrect and ensure the metric is not increased twice.
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/2,
                              /*reverted=*/0, /*accepted=*/1,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       OnBlurDoesNotRecordMetricsForStaleAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  manager_.OnSurroundingTextChanged(u"the abc", 7, 7);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/1,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/0);

  // Set stale autocorrect range.
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(0, 3),
                                                      base::DoNothing());

  // Handle a new autocorrect and ensure the metric is not increase twice.
  manager_.OnBlur();
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/1,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       OnFocusDoesNotRecordMetricsForStaleAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  // Accept autocorrect implicitly.
  manager_.OnBlur();
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/1);

  // Set stale autocorrect range.
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(0, 3),
                                                      base::DoNothing());

  // Handle a new autocorrect and ensure the metric is not increase twice.
  manager_.OnFocus(1);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/1);
}

TEST_F(AutocorrectManagerTest, ImplicitAcceptanceClearsAutocorrectRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 7), u"smeone", u"someone");

  manager_.OnSurroundingTextChanged(u"someone ", 8, 8);

  // Ensure range is as expected.
  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(0, 7));

  // Implicitly accept autocorrect by three character insertion.
  manager_.OnSurroundingTextChanged(u"someone abc", 11, 11);

  EXPECT_TRUE(mock_ime_input_context_handler_.GetAutocorrectRange().is_empty());
}

TEST_F(AutocorrectManagerTest, AsyncDelayDoesNotMakeAutocorrectAccepted) {
  // To commit autocorrect, a delete operation is first applied then an insert.
  // In the case of async delay, the surrounding text related to each of these
  // operations might be received after handling the range but needs to be
  // ignored by validation process.

  manager_.HandleAutocorrect(gfx::Range(0, 7), u"smeone", u"someone");

  // Late surrounding text related to IME delete.
  manager_.OnSurroundingTextChanged(u"s ", 1, 1);
  // Late surrounding text related to IME insert.
  manager_.OnSurroundingTextChanged(u"someone ", 8, 8);

  // Autocorrect range is not cleared by the stale surrounding text.
  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(0, 7));
}

TEST_F(AutocorrectManagerTest,
       ImplicitAcceptanceRecordsMetricsAndIgnoresAsyncStaleData) {
  manager_.HandleAutocorrect(gfx::Range(0, 7), u"smeone", u"someone");

  // Late surrounding text related to IME delete.
  manager_.OnSurroundingTextChanged(u"s ", 1, 1);
  // Late surrounding text related to IME insert.
  manager_.OnSurroundingTextChanged(u"someone ", 8, 8);
  // User adds two characters.
  manager_.OnSurroundingTextChanged(u"someone ab", 10, 10);
  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(0, 7));

  // Third character, implicitly accepts autocorrect.
  manager_.OnSurroundingTextChanged(u"someone abc", 11, 11);

  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/1,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       RecordMetricsForVkWhenVkWasVisibleAtUnderlineTime) {
  // VK is visible at the time of suggesting an autocorrect.
  keyboard_client_->set_keyboard_visible_for_test(true);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // To suppress strict mock.
  EXPECT_CALL(mock_suggestion_handler_, SetAssistiveWindowProperties(_, _, _));

  // VK is made hidden, but still the metrics need to be recorded for VK
  // given VK was visible at underline time.
  keyboard_client_->set_keyboard_visible_for_test(false);
  manager_.OnSurroundingTextChanged(u"the ", 1, 1);

  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/true,
                              /*window_shown=*/1, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       DoesNotRecordMetricsForVkWhenVkWasNotVisibleAtUnderlineTime) {
  // VK is not visible at the time of suggesting an autocorrect.
  keyboard_client_->set_keyboard_visible_for_test(false);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // To suppress strict mock.
  EXPECT_CALL(mock_suggestion_handler_, SetAssistiveWindowProperties(_, _, _));

  // VK is made visible, but still metrics must not be recorded for VK
  // as it was not visible at the time of underline.
  keyboard_client_->set_keyboard_visible_for_test(true);
  manager_.OnSurroundingTextChanged(u"the ", 1, 1);

  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/1, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest, UndoRecordsMetricsAfterRevert) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  manager_.UndoAutocorrect();
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/1, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/0);
}

TEST_F(AutocorrectManagerTest, HandleAutocorrectRecordsMetricsWhenVkIsVisible) {
  keyboard_client_->set_keyboard_visible_for_test(true);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/true,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest, ExitingTextFieldRecordsMetricsWhenVkIsVisible) {
  keyboard_client_->set_keyboard_visible_for_test(true);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnBlur();
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/true,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/1);
}

TEST_F(AutocorrectManagerTest,
       AcceptingAutocorrectRecordsMetricsWhenVkIsVisible) {
  keyboard_client_->set_keyboard_visible_for_test(true);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  // Implicitly accept autocorrect
  manager_.OnSurroundingTextChanged(u"the abc", 7, 7);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/true,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/1,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/0);
}

TEST_F(AutocorrectManagerTest, ThreeValidationFailuresDoesNotClearRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Three validation failures.
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);

  // Range is not cleared.
  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(0, 3));
}

TEST_F(AutocorrectManagerTest, FourValidationFailuresClearsRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Four validation failure.
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);

  EXPECT_TRUE(mock_ime_input_context_handler_.GetAutocorrectRange().is_empty());
}

TEST_F(AutocorrectManagerTest, InvalidRangeFailsValidationAndClearsRange) {
  manager_.HandleAutocorrect(gfx::Range(2, 5), u"teh", u"the");

  // Four validation failure because the range is invalid.
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  EXPECT_TRUE(mock_ime_input_context_handler_.GetAutocorrectRange().is_empty());
}

TEST_F(AutocorrectManagerTest,
       FourValidationFailuresRecordsMetricsForInvalidRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Four validation failure.
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);

  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/0,
                              /*invalid_range*/ 1);
}

TEST_F(AutocorrectManagerTest, UndoRecordsMetricsWhenVkIsVisible) {
  keyboard_client_->set_keyboard_visible_for_test(true);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  manager_.UndoAutocorrect();
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/true,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/1, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/0);
}

TEST_F(AutocorrectManagerTest,
       ClearingAutocorrectRecordsMetricsWhenVkIsVisible) {
  keyboard_client_->set_keyboard_visible_for_test(true);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);

  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/true,
                              /*window_shown=*/0, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/1,
                              /*exited_text_field_with_underline=*/0);
}

TEST_F(AutocorrectManagerTest, ConsistentAsyncDelayClearsRangeIncorrectly) {
  // This is a case that if happens in practice will cause the Autocorrect
  // logic to fail. Here, imagine that with any user input, autocorrect range
  // is updated in the text input client with a delay. So, validation never
  // succeeds because the range is always one step old.

  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // Each OnSurroundingTextChanged is received with stale autocorrect range
  // belonging to the previous state.
  manager_.OnSurroundingTextChanged(u"athe ", 1, 1);
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(1, 4),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"abthe ", 2, 2);
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(2, 5),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"abcthe ", 3, 3);
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(3, 6),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"abcdthe ", 4, 4);

  // Expect that the validation fails.
  EXPECT_TRUE(mock_ime_input_context_handler_.GetAutocorrectRange().is_empty());
}

TEST_F(AutocorrectManagerTest,
       AutocorrectIsIncorrectlyAcceptedWhenStaleRangeAndNewSuggestionMatch) {
  // This is a case that has no solution to prevent and can result in
  // an incorrect autocorrect behaviour when happening.

  manager_.HandleAutocorrect(gfx::Range(0, 4), u"ths", u"this");
  manager_.HandleAutocorrect(gfx::Range(5, 9), u"ths", u"this");

  // Set stale autocorrect range.
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(0, 4),
                                                      base::DoNothing());

  // Surrounding text changed is stale (updated one is 'this this').
  // The range is now validated because stale range matches the new suggestion.
  manager_.OnSurroundingTextChanged(u"this t", 6, 6);

  // Updated surrounding text counts three insertions.
  manager_.OnSurroundingTextChanged(u"this this ", 10, 10);

  // The range is accepted incorrectly.
  EXPECT_TRUE(mock_ime_input_context_handler_.GetAutocorrectRange().is_empty());
}

TEST_F(AutocorrectManagerTest, InvalidOriginalTextArgClearsRange) {
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(0, 3),
                                                      base::DoNothing());
  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(0, 3));

  // Original text is empty and invalid.
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"", u"the");
  EXPECT_TRUE(mock_ime_input_context_handler_.GetAutocorrectRange().is_empty());
}

TEST_F(AutocorrectManagerTest, InvalidOriginalTextArgDoesNotRecordMetrics) {
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(0, 3),
                                                      base::DoNothing());
  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(0, 3));

  manager_.HandleAutocorrect(gfx::Range(0, 3), u"", u"the");
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/0,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/0);
}

TEST_F(AutocorrectManagerTest, EmptySuggestedTextArgClearsRange) {
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(0, 3),
                                                      base::DoNothing());
  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(0, 3));

  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"");
  EXPECT_TRUE(mock_ime_input_context_handler_.GetAutocorrectRange().is_empty());
}

TEST_F(AutocorrectManagerTest, EmptySuggestedTextArgDoesNotRecordMetrics) {
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(0, 3),
                                                      base::DoNothing());
  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(0, 3));

  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"");
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/0,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/0);
}

TEST_F(AutocorrectManagerTest, RangeAndSuggestionMismatchDoesNotRecordMetrics) {
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(0, 3),
                                                      base::DoNothing());
  EXPECT_EQ(mock_ime_input_context_handler_.GetAutocorrectRange(),
            gfx::Range(0, 3));

  manager_.HandleAutocorrect(gfx::Range(0, 4), u"teh", u"");
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/false,
                              /*window_shown=*/0, /*underlined=*/0,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0,
                              /*exited_text_field_with_underline=*/0);
}

TEST_F(AutocorrectManagerTest, ShowingUndoWindowRecordsMetricsWhenVkIsVisible) {
  keyboard_client_->set_keyboard_visible_for_test(true);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // This suppresses strict mock.
  EXPECT_CALL(mock_suggestion_handler_, SetAssistiveWindowProperties(_, _, _));

  manager_.OnSurroundingTextChanged(u"the", 0, 0);
  ExpectAutocorrectHistograms(histogram_tester_, /*visible_vk=*/true,
                              /*window_shown=*/1, /*underlined=*/1,
                              /*reverted=*/0, /*accepted=*/0,
                              /*cleared_underline=*/0);
}

TEST_F(AutocorrectManagerTest, RecordQualityBreakdownForAccentChange) {
  manager_.HandleAutocorrect(gfx::Range(0, 8), u"francais", u"français");

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"français ", 9, 9);
  manager_.OnSurroundingTextChanged(u"français abc", 12, 12);

  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionChangedAccent, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionMutatedLetters, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kOriginalTextIsAscii, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionResolved, 1);
  histogram_tester_.ExpectTotalCount(kAutocorrectV2QualityPkAcceptedHistName,
                                     4);
}

TEST_F(AutocorrectManagerTest, RecordQualityBreakdownForWordSplit) {
  manager_.HandleAutocorrect(gfx::Range(0, 11), u"helloworld", u"hello world");

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"hello world ", 12, 12);
  manager_.OnSurroundingTextChanged(u"hello world abc", 15, 15);

  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionSplittedWord, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionInsertedLetters, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kOriginalTextIsAscii, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestedTextIsAscii, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionResolved, 1);
  histogram_tester_.ExpectTotalCount(kAutocorrectV2QualityPkAcceptedHistName,
                                     5);
}

TEST_F(AutocorrectManagerTest, RecordQualityBreakdownForRemovingLetters) {
  manager_.HandleAutocorrect(gfx::Range(0, 8), u"françaisss", u"français");

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"français ", 9, 9);
  manager_.OnSurroundingTextChanged(u"français abc", 12, 12);

  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionRemovedLetters, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionResolved, 1);
  histogram_tester_.ExpectTotalCount(kAutocorrectV2QualityPkAcceptedHistName,
                                     2);
}

TEST_F(AutocorrectManagerTest, RecordQualityBreakdownForCapitalizedWorld) {
  manager_.HandleAutocorrect(gfx::Range(0, 1), u"i", u"I");

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"I ", 2, 2);
  manager_.OnSurroundingTextChanged(u"I have", 6, 6);

  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionCapitalizedWord, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kOriginalTextIsAscii, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestedTextIsAscii, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionMutatedLetters, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionChangeLetterCases, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionResolved, 1);
  histogram_tester_.ExpectTotalCount(kAutocorrectV2QualityPkAcceptedHistName,
                                     6);
}

TEST_F(AutocorrectManagerTest, RecordQualityBreakdownForLowerCasedLetter) {
  manager_.HandleAutocorrect(gfx::Range(0, 8), u"Français", u"français");

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"français ", 9, 9);
  manager_.OnSurroundingTextChanged(u"français abc", 12, 12);

  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionLowerCasedWord, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionResolved, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionMutatedLetters, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionChangeLetterCases, 1);
  histogram_tester_.ExpectTotalCount(kAutocorrectV2QualityPkAcceptedHistName,
                                     4);
}

TEST_F(AutocorrectManagerTest, RecordQualityBreakdownForVkAccepted) {
  keyboard_client_->set_keyboard_visible_for_test(true);
  manager_.HandleAutocorrect(gfx::Range(0, 8), u"françaisss", u"français");

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"français ", 9, 9);
  manager_.OnSurroundingTextChanged(u"français abc", 12, 12);

  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityVkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionRemovedLetters, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityVkAcceptedHistName,
      AutocorrectQualityBreakdown::kSuggestionResolved, 1);
  histogram_tester_.ExpectTotalCount(kAutocorrectV2QualityVkAcceptedHistName,
                                     2);
}

TEST_F(AutocorrectManagerTest, RecordQualityBreakdownForVkRejected) {
  keyboard_client_->set_keyboard_visible_for_test(true);
  manager_.HandleAutocorrect(gfx::Range(0, 8), u"françaisss", u"français");

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"français ", 9, 9);
  // Clear range.
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"franças ", 8, 8);

  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityVkRejectedHistName,
      AutocorrectQualityBreakdown::kSuggestionRemovedLetters, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityVkRejectedHistName,
      AutocorrectQualityBreakdown::kSuggestionResolved, 1);
  histogram_tester_.ExpectTotalCount(kAutocorrectV2QualityVkRejectedHistName,
                                     2);
}

TEST_F(AutocorrectManagerTest, RecordQualityBreakdownForPkRejected) {
  manager_.HandleAutocorrect(gfx::Range(0, 8), u"françaisss", u"français");

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"français ", 9, 9);
  // Clear range.
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"franças ", 8, 8);

  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkRejectedHistName,
      AutocorrectQualityBreakdown::kSuggestionRemovedLetters, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2QualityPkRejectedHistName,
      AutocorrectQualityBreakdown::kSuggestionResolved, 1);
  histogram_tester_.ExpectTotalCount(kAutocorrectV2QualityPkRejectedHistName,
                                     2);
}

TEST_F(AutocorrectManagerTest, RecordDistanceMetricForVkAccepted) {
  keyboard_client_->set_keyboard_visible_for_test(true);
  manager_.HandleAutocorrect(gfx::Range(0, 4), u"cafe", u"cafè");
  // (|cafe|-1) * MAX_LENGTH + (|{'e'->'è'}| - 1)
  int expected_value = (4 - 1) * 30 + (1 - 1);

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"cafè ", 5, 5);
  manager_.OnSurroundingTextChanged(u"cafè abc", 8, 8);

  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2Distance2dVkAcceptedHistName, expected_value, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2DistanceSuggestedVkAcceptedHistName, 4, 1);
  histogram_tester_.ExpectTotalCount(kAutocorrectV2Distance2dVkAcceptedHistName,
                                     1);
  histogram_tester_.ExpectTotalCount(
      kAutocorrectV2DistanceSuggestedVkAcceptedHistName, 1);
}

TEST_F(AutocorrectManagerTest, RecordDistanceMetricForPkAccepted) {
  manager_.HandleAutocorrect(gfx::Range(0, 1), u"i", u"I");
  //  (|i|-1) * MAX_LENGTH + (|{'i'->'I'}| - 1)
  int expected_value = (1 - 1) * 30 + (1 - 1);

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"I ", 2, 2);
  manager_.OnSurroundingTextChanged(u"I abc", 5, 5);

  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2Distance2dPkAcceptedHistName, expected_value, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2DistanceSuggestedPkAcceptedHistName, 1, 1);
  histogram_tester_.ExpectTotalCount(kAutocorrectV2Distance2dPkAcceptedHistName,
                                     1);
  histogram_tester_.ExpectTotalCount(
      kAutocorrectV2DistanceSuggestedPkAcceptedHistName, 1);
}

TEST_F(AutocorrectManagerTest, RecordDistanceMetricForVkRejected) {
  keyboard_client_->set_keyboard_visible_for_test(true);
  manager_.HandleAutocorrect(gfx::Range(0, 12), u"ecauserthy", u"because they");
  //  (|ecauserthy|-1) * MAX_LENGTH + (|{''->'b'}, {'r'->' '}, {''->'e'}| - 1)
  int expected_value = (10 - 1) * 30 + (3 - 1);

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"because they ", 13, 13);
  // Clear range.
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(u"because ", 8, 8);

  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2Distance2dVkRejectedHistName, expected_value, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2DistanceSuggestedVkRejectedHistName, 12, 1);
  histogram_tester_.ExpectTotalCount(kAutocorrectV2Distance2dVkRejectedHistName,
                                     1);
  histogram_tester_.ExpectTotalCount(
      kAutocorrectV2DistanceSuggestedVkRejectedHistName, 1);
}

TEST_F(AutocorrectManagerTest, RecordDistanceMetricForPkRejected) {
  manager_.HandleAutocorrect(
      gfx::Range(0, 42),
      u"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      u"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  //  (min(|<word1>|, MAX_LENGTH) - 1) * MAX_LENGTH +
  //                          (min(<number of changes>, MAX_LENGTH) - 1)
  int expected_value = (30 - 1) * 30 + (30 - 1);

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(
      u"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb ", 43, 43);
  // Clear range.
  mock_ime_input_context_handler_.SetAutocorrectRange(gfx::Range(),
                                                      base::DoNothing());
  manager_.OnSurroundingTextChanged(
      u"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa ", 55, 55);

  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2Distance2dPkRejectedHistName, expected_value, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2DistanceSuggestedPkRejectedHistName, 30, 1);
  histogram_tester_.ExpectTotalCount(kAutocorrectV2Distance2dPkRejectedHistName,
                                     1);
  histogram_tester_.ExpectTotalCount(
      kAutocorrectV2DistanceSuggestedPkRejectedHistName, 1);
}

TEST_F(AutocorrectManagerTest, DistanceMetricNoChange) {
  manager_.HandleAutocorrect(gfx::Range(0, 9), u"no change", u"no change");
  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"no change ", 10, 10);
  manager_.OnSurroundingTextChanged(u"no change abc", 13, 13);
  manager_.HandleAutocorrect(gfx::Range(0, 9), u"", u"not empty");

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"not empty ", 10, 10);
  manager_.OnSurroundingTextChanged(u"not empty abc", 13, 13);

  histogram_tester_.ExpectTotalCount(
      kAutocorrectV2DistanceSuggestedPkAcceptedHistName, 0);
  histogram_tester_.ExpectTotalCount(
      kAutocorrectV2DistanceSuggestedPkAcceptedHistName, 0);
}

TEST_F(AutocorrectManagerTest, RecordDistanceMetricNoOverlap) {
  manager_.HandleAutocorrect(gfx::Range(0, 32), u"aaaa",
                             u"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  //  (|aaaa|-1) * MAX_LENGTH + (min(<number of changes>, MAX_LENGTH) - 1)
  int expected_value1 = (4 - 1) * 30 + (30 - 1);
  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb ", 33,
                                    33);
  manager_.OnSurroundingTextChanged(u"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb abc", 35,
                                    35);

  manager_.HandleAutocorrect(gfx::Range(0, 4), u"aaaaa", u"aaaa");
  //  (|aaaaa|-1) * MAX_LENGTH + (|{'a'->''}| - 1)
  int expected_value2 = (5 - 1) * 30 + (1 - 1);
  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"aaaa ", 5, 5);
  manager_.OnSurroundingTextChanged(u"aaaa abc", 8, 8);

  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2Distance2dPkAcceptedHistName, expected_value1, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2Distance2dPkAcceptedHistName, expected_value2, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2DistanceSuggestedPkAcceptedHistName, 30, 1);
  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2DistanceSuggestedPkAcceptedHistName, 4, 1);
  histogram_tester_.ExpectTotalCount(kAutocorrectV2Distance2dPkAcceptedHistName,
                                     2);
  histogram_tester_.ExpectTotalCount(
      kAutocorrectV2DistanceSuggestedPkAcceptedHistName, 2);
}

TEST_F(AutocorrectManagerTest, RecordDistanceMetricAlmostMaxLength) {
  manager_.HandleAutocorrect(gfx::Range(0, 1), u"iiiiiiiiiiiiiiiiiiiiiiiiiiiii",
                             u"I");
  // (|<word1>| - 1) * MAX_LENGTH + (<number of changes> - 1)
  int expected_value = (29 - 1) * 30 + (29 - 1);

  // Accept autocorrect implicitly.
  manager_.OnSurroundingTextChanged(u"I ", 2, 2);
  manager_.OnSurroundingTextChanged(u"I abc", 5, 5);

  histogram_tester_.ExpectBucketCount(
      kAutocorrectV2Distance2dPkAcceptedHistName, expected_value, 1);
  histogram_tester_.ExpectTotalCount(kAutocorrectV2Distance2dPkAcceptedHistName,
                                     1);
}

struct PkUserPrefCase {
  std::string test_name;
  std::string engine_id;
  bool autocorrect_enabled;
  bool vk_visible;
  absl::optional<AutocorrectPreference> expected_all_pref;
  absl::optional<AutocorrectPreference> expected_eng_pref;
};

class PkUserPreferenceMetric
    : public AutocorrectManagerTest,
      public testing::WithParamInterface<PkUserPrefCase> {};

TEST_P(PkUserPreferenceMetric, IsNotRecordedWhenKeyEventNotEncountered) {
  const PkUserPrefCase& test_case = GetParam();
  SetAutocorrectPreferenceTo(/*profile=*/*profile_,
                             /*engine_id=*/test_case.engine_id,
                             /*enabled=*/test_case.autocorrect_enabled);

  manager_.OnActivate(test_case.engine_id);
  manager_.OnFocus(kContextId);

  histogram_tester_.ExpectTotalCount(kAutocorrectV2PkUserPreferenceAll, 0);
  histogram_tester_.ExpectTotalCount(kAutocorrectV2PkUserPreferenceEnglish, 0);
}

TEST_P(PkUserPreferenceMetric, IsRecordedCorrectlyAfterOnFocusThenOnKeyEvent) {
  const PkUserPrefCase& test_case = GetParam();
  keyboard_client_->set_keyboard_visible_for_test(test_case.vk_visible);
  SetAutocorrectPreferenceTo(/*profile=*/*profile_,
                             /*engine_id=*/test_case.engine_id,
                             /*enabled=*/test_case.autocorrect_enabled);

  manager_.OnActivate(test_case.engine_id);
  manager_.OnFocus(kContextId);
  manager_.OnKeyEvent(KeyA());
  manager_.OnFocus(kContextId);
  manager_.OnKeyEvent(KeyA());
  manager_.OnKeyEvent(KeyA());

  // There are two successive OnFocus then OnKeyEvent sequences above. Thus we
  // expect that there should be two recordings of the metric.
  int expected_all_count = test_case.expected_all_pref ? 2 : 0;
  int expected_eng_count = test_case.expected_eng_pref ? 2 : 0;

  histogram_tester_.ExpectTotalCount(kAutocorrectV2PkUserPreferenceAll,
                                     expected_all_count);
  histogram_tester_.ExpectTotalCount(kAutocorrectV2PkUserPreferenceEnglish,
                                     expected_eng_count);
  if (test_case.expected_all_pref) {
    histogram_tester_.ExpectBucketCount(kAutocorrectV2PkUserPreferenceAll,
                                        test_case.expected_all_pref.value(),
                                        expected_all_count);
  }

  if (test_case.expected_eng_pref) {
    histogram_tester_.ExpectBucketCount(kAutocorrectV2PkUserPreferenceEnglish,
                                        test_case.expected_eng_pref.value(),
                                        expected_eng_count);
  }
}

INSTANTIATE_TEST_SUITE_P(
    AutocorrectManagerTest,
    PkUserPreferenceMetric,
    testing::ValuesIn<PkUserPrefCase>({
        // US_ENGLISH examples
        {"UsEnglishEnabled",
         /*engine_id=*/kUsEnglishEngineId,
         /*autocorrect_enabled=*/true,
         /*vk_visible=*/false,
         /*expected_all_pref=*/AutocorrectPreference::kEnabled,
         /*expected_eng_pref=*/AutocorrectPreference::kEnabled},
        {"UsEnglishDisabled",
         /*engine_id=*/kUsEnglishEngineId,
         /*autocorrect_enabled=*/false,
         /*vk_visible=*/false,
         /*expected_all_pref=*/AutocorrectPreference::kDisabled,
         /*expected_eng_pref=*/AutocorrectPreference::kDisabled},
        {"UsEnglishNotRecordedWithVK",
         /*engine_id=*/kUsEnglishEngineId,
         /*autocorrect_enabled=*/true,
         /*vk_visible=*/true,
         /*expected_all_pref=*/absl::nullopt,
         /*expected_eng_pref=*/absl::nullopt},

        // ALL EXAMPLES
        {"UsInternationalEnabled",
         /*engine_id=*/kUsInternationalEngineId,
         /*autocorrect_enabled=*/true,
         /*vk_visible=*/false,
         /*expected_all_pref=*/AutocorrectPreference::kEnabled,
         /*expected_eng_pref=*/absl::nullopt},
        {"UsInternationalDisabled",
         /*engine_id=*/kUsInternationalEngineId,
         /*autocorrect_enabled=*/false,
         /*vk_visible=*/false,
         /*expected_all_pref=*/AutocorrectPreference::kDisabled,
         /*expected_eng_pref=*/absl::nullopt},
        {"UsInternationalNotRecordedWithVK",
         /*engine_id=*/kUsInternationalEngineId,
         /*autocorrect_enabled=*/true,
         /*vk_visible=*/true,
         /*expected_all_pref=*/absl::nullopt,
         /*expected_eng_pref=*/absl::nullopt},

        {"SpainSpanishEnabled",
         /*engine_id=*/kSpainSpanishEngineId,
         /*autocorrect_enabled=*/true,
         /*vk_visible=*/false,
         /*expected_all_pref=*/AutocorrectPreference::kEnabled,
         /*expected_eng_pref=*/absl::nullopt},
        {"SpainSpanishDisabled",
         /*engine_id=*/kSpainSpanishEngineId,
         /*autocorrect_enabled=*/false,
         /*vk_visible=*/false,
         /*expected_all_pref=*/AutocorrectPreference::kDisabled,
         /*expected_eng_pref=*/absl::nullopt},
        {"SpainSpanishNotRecordedWithVK",
         /*engine_id=*/kSpainSpanishEngineId,
         /*autocorrect_enabled=*/true,
         /*vk_visible=*/true,
         /*expected_all_pref=*/absl::nullopt,
         /*expected_eng_pref=*/absl::nullopt},

        {"LatinAmericaSpanishEnabled",
         /*engine_id=*/kLatinAmericaSpanishEngineId,
         /*autocorrect_enabled=*/true,
         /*vk_visible=*/false,
         /*expected_all_pref=*/AutocorrectPreference::kEnabled,
         /*expected_eng_pref=*/absl::nullopt},
        {"LatinAmericaSpanishDisabled",
         /*engine_id=*/kLatinAmericaSpanishEngineId,
         /*autocorrect_enabled=*/false,
         /*vk_visible=*/false,
         /*expected_all_pref=*/AutocorrectPreference::kDisabled,
         /*expected_eng_pref=*/absl::nullopt},
        {"LatinAmericaNotRecordedWithVK",
         /*engine_id=*/kLatinAmericaSpanishEngineId,
         /*autocorrect_enabled=*/true,
         /*vk_visible=*/true,
         /*expected_all_pref=*/absl::nullopt,
         /*expected_eng_pref=*/absl::nullopt},

        {"BrazilPortugeseEnabled",
         /*engine_id=*/kBrazilPortugeseEngineId,
         /*autocorrect_enabled=*/true,
         /*vk_visible=*/false,
         /*expected_all_pref=*/AutocorrectPreference::kEnabled,
         /*expected_eng_pref=*/absl::nullopt},
        {"BrazilPortugeseDisabled",
         /*engine_id=*/kBrazilPortugeseEngineId,
         /*autocorrect_enabled=*/false,
         /*vk_visible=*/false,
         /*expected_all_pref=*/AutocorrectPreference::kDisabled,
         /*expected_eng_pref=*/absl::nullopt},
        {"BrazilPortugeseNotRecordedWithVK",
         /*engine_id=*/kBrazilPortugeseEngineId,
         /*autocorrect_enabled=*/true,
         /*vk_visible=*/true,
         /*expected_all_pref=*/absl::nullopt,
         /*expected_eng_pref=*/absl::nullopt},

        {"FranceFrenchEnabled",
         /*engine_id=*/kFranceFrenchEngineId,
         /*autocorrect_enabled=*/true,
         /*vk_visible=*/false,
         /*expected_all_pref=*/AutocorrectPreference::kEnabled,
         /*expected_eng_pref=*/absl::nullopt},
        {"FranceFrenchDisabled",
         /*engine_id=*/kFranceFrenchEngineId,
         /*autocorrect_enabled=*/false,
         /*vk_visible=*/false,
         /*expected_all_pref=*/AutocorrectPreference::kDisabled,
         /*expected_eng_pref=*/absl::nullopt},
        {"FranceFrenchNotRecordedWithVK",
         /*engine_id=*/kFranceFrenchEngineId,
         /*autocorrect_enabled=*/true,
         /*vk_visible=*/true,
         /*expected_all_pref=*/absl::nullopt,
         /*expected_eng_pref=*/absl::nullopt},
    }),
    [](const testing::TestParamInfo<PkUserPrefCase> info) {
      return info.param.test_name;
    });

class AutocorrectManagerUkmMetricsTest : public AutocorrectManagerTest {
 protected:
  AutocorrectManagerUkmMetricsTest() {
    ukm::SourceId source_id = test_recorder_.GetNewSourceID();
    test_recorder_.UpdateSourceURL(source_id,
                                   GURL("https://test.example.com/"));

    fake_text_input_client_.set_source_id(source_id);
    ui::IMEBridge::Get()->SetInputContextHandler(&mock_input_method_ash_);

    mock_input_method_ash_.SetFocusedTextInputClient(&fake_text_input_client_);
  }

  ui::FakeTextInputClient fake_text_input_client_{ui::TEXT_INPUT_TYPE_TEXT};
  ui::InputMethodAsh mock_input_method_ash_{nullptr};
  ukm::TestAutoSetUkmRecorder test_recorder_;
};

TEST_F(AutocorrectManagerUkmMetricsTest,
       RecordsAppCompatUkmForUnderlinedSuggestion) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  auto ukm_entries = test_recorder_.GetEntriesByName(UkmEntry::kEntryName);
  EXPECT_EQ(1u, ukm_entries.size());
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[0], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(AutocorrectCompatibilitySummary::kUnderlined));
}

TEST_F(AutocorrectManagerUkmMetricsTest,
       DoesNotRecordsAppCompatUkmForInvalidSourceId) {
  fake_text_input_client_.set_source_id(ukm::kInvalidSourceId);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  auto ukm_entries = test_recorder_.GetEntriesByName(UkmEntry::kEntryName);
  EXPECT_EQ(0u, ukm_entries.size());
}

TEST_F(AutocorrectManagerUkmMetricsTest,
       RecordsAppCompatUkmForVKUnderlinedSuggestion) {
  keyboard_client_->set_keyboard_visible_for_test(true);
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  auto ukm_entries = test_recorder_.GetEntriesByName(UkmEntry::kEntryName);
  EXPECT_EQ(1u, ukm_entries.size());
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[0], UkmEntry::kCompatibilitySummary_VKName,
      static_cast<int>(AutocorrectCompatibilitySummary::kUnderlined));
}

TEST_F(AutocorrectManagerUkmMetricsTest, RecordsAppCompatUkmForInvalidRange) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  task_environment_.FastForwardBy(base::Milliseconds(501));

  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);

  auto ukm_entries = test_recorder_.GetEntriesByName(UkmEntry::kEntryName);
  EXPECT_EQ(2u, ukm_entries.size());
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[1], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(AutocorrectCompatibilitySummary::kInvalidRange));
}

TEST_F(AutocorrectManagerUkmMetricsTest,
       RecordsAppCompatUkmForRevertedSuggestion) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  task_environment_.FastForwardBy(base::Milliseconds(501));

  manager_.UndoAutocorrect();

  auto ukm_entries = test_recorder_.GetEntriesByName(UkmEntry::kEntryName);
  EXPECT_EQ(2u, ukm_entries.size());
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[1], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(AutocorrectCompatibilitySummary::kReverted));
}

TEST_F(AutocorrectManagerUkmMetricsTest, RecordsAppCompatUkmForWindowShown) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");

  // This suppresses strict mock.
  EXPECT_CALL(mock_suggestion_handler_, SetAssistiveWindowProperties(_, _, _));

  manager_.OnSurroundingTextChanged(u"the ", 0, 0);

  auto ukm_entries = test_recorder_.GetEntriesByName(UkmEntry::kEntryName);
  EXPECT_EQ(2u, ukm_entries.size());
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[1], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(AutocorrectCompatibilitySummary::kWindowShown));
}

TEST_F(AutocorrectManagerUkmMetricsTest,
       RecordsAppCompatUkmForVeryFastAcceptedSuggestion) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  task_environment_.FastForwardBy(base::Milliseconds(200));

  // Implicitly accept autocorrect.
  manager_.OnSurroundingTextChanged(u"the abc", 7, 7);

  auto ukm_entries = test_recorder_.GetEntriesByName(UkmEntry::kEntryName);
  EXPECT_EQ(3u, ukm_entries.size());
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[1], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(
          AutocorrectCompatibilitySummary::kUserAcceptedAutocorrect));
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[2], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(
          AutocorrectCompatibilitySummary::kVeryFastAcceptedAutocorrect));
}

TEST_F(AutocorrectManagerUkmMetricsTest,
       RecordsAppCompatUkmForFastAcceptedSuggestion) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  task_environment_.FastForwardBy(base::Milliseconds(500));

  // Implicitly accept autocorrect.
  manager_.OnSurroundingTextChanged(u"the abc", 7, 7);

  auto ukm_entries = test_recorder_.GetEntriesByName(UkmEntry::kEntryName);
  EXPECT_EQ(3u, ukm_entries.size());
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[1], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(
          AutocorrectCompatibilitySummary::kUserAcceptedAutocorrect));
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[2], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(
          AutocorrectCompatibilitySummary::kFastAcceptedAutocorrect));
}

TEST_F(AutocorrectManagerUkmMetricsTest,
       RecordsAppCompatUkmForAcceptedSuggestion) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  task_environment_.FastForwardBy(base::Milliseconds(501));

  // Implicitly accept autocorrect.
  manager_.OnSurroundingTextChanged(u"the abc", 7, 7);

  auto ukm_entries = test_recorder_.GetEntriesByName(UkmEntry::kEntryName);
  EXPECT_EQ(2u, ukm_entries.size());
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[1], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(
          AutocorrectCompatibilitySummary::kUserAcceptedAutocorrect));
}

TEST_F(AutocorrectManagerUkmMetricsTest,
       RecordsAppCompatUkmForVeryFastRejectedSuggestion) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  task_environment_.FastForwardBy(base::Milliseconds(200));

  // Clear the range.
  mock_input_method_ash_.SetAutocorrectRange(gfx::Range(), base::DoNothing());
  // Process the cleared range ('the' is mutated to implicitly reject it).
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);

  auto ukm_entries = test_recorder_.GetEntriesByName(UkmEntry::kEntryName);
  EXPECT_EQ(3u, ukm_entries.size());
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[1], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(
          AutocorrectCompatibilitySummary::kUserActionClearedUnderline));
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[2], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(
          AutocorrectCompatibilitySummary::kVeryFastRejectedAutocorrect));
}

TEST_F(AutocorrectManagerUkmMetricsTest,
       RecordsAppCompatUkmForFastRejectedSuggestion) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  task_environment_.FastForwardBy(base::Milliseconds(500));

  // Clear the range.
  mock_input_method_ash_.SetAutocorrectRange(gfx::Range(), base::DoNothing());
  // Process the cleared range ('the' is mutated to implicitly reject it).
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);

  auto ukm_entries = test_recorder_.GetEntriesByName(UkmEntry::kEntryName);
  EXPECT_EQ(3u, ukm_entries.size());
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[1], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(
          AutocorrectCompatibilitySummary::kUserActionClearedUnderline));
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[2], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(
          AutocorrectCompatibilitySummary::kFastRejectedAutocorrect));
}

TEST_F(AutocorrectManagerUkmMetricsTest,
       RecordsAppCompatUkmForRejectedSuggestion) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  task_environment_.FastForwardBy(base::Milliseconds(501));

  // Clear the range.
  mock_input_method_ash_.SetAutocorrectRange(gfx::Range(), base::DoNothing());
  // Process the cleared range ('the' is mutated to implicitly reject it).
  manager_.OnSurroundingTextChanged(u"teh ", 4, 4);

  auto ukm_entries = test_recorder_.GetEntriesByName(UkmEntry::kEntryName);
  EXPECT_EQ(2u, ukm_entries.size());
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[1], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(
          AutocorrectCompatibilitySummary::kUserActionClearedUnderline));
}

TEST_F(AutocorrectManagerUkmMetricsTest,
       RecordsAppCompatUkmForVeryFastExitField) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  task_environment_.FastForwardBy(base::Milliseconds(200));
  manager_.OnBlur();

  auto ukm_entries = test_recorder_.GetEntriesByName(UkmEntry::kEntryName);
  EXPECT_EQ(3u, ukm_entries.size());
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[1], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(
          AutocorrectCompatibilitySummary::kUserExitedTextFieldWithUnderline));
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[2], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(AutocorrectCompatibilitySummary::kVeryFastExitField));
}

TEST_F(AutocorrectManagerUkmMetricsTest, RecordsAppCompatUkmForFastExitField) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  task_environment_.FastForwardBy(base::Milliseconds(500));
  manager_.OnBlur();

  auto ukm_entries = test_recorder_.GetEntriesByName(UkmEntry::kEntryName);
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[1], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(
          AutocorrectCompatibilitySummary::kUserExitedTextFieldWithUnderline));
  EXPECT_EQ(3u, ukm_entries.size());
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[2], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(AutocorrectCompatibilitySummary::kFastExitField));
}

TEST_F(AutocorrectManagerUkmMetricsTest, RecordsAppCompatUkmForExitField) {
  manager_.HandleAutocorrect(gfx::Range(0, 3), u"teh", u"the");
  manager_.OnSurroundingTextChanged(u"the ", 4, 4);

  task_environment_.FastForwardBy(base::Milliseconds(501));
  manager_.OnBlur();

  auto ukm_entries = test_recorder_.GetEntriesByName(UkmEntry::kEntryName);
  EXPECT_EQ(2u, ukm_entries.size());
  ukm::TestAutoSetUkmRecorder::ExpectEntryMetric(
      ukm_entries[1], UkmEntry::kCompatibilitySummary_PKName,
      static_cast<int>(
          AutocorrectCompatibilitySummary::kUserExitedTextFieldWithUnderline));
}

}  // namespace
}  // namespace input_method
}  // namespace ash
