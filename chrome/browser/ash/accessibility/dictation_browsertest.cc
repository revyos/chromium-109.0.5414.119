// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/accessibility/dictation.h"

#include <memory>

#include "ash/accessibility/accessibility_controller_impl.h"
#include "ash/constants/ash_pref_names.h"
#include "ash/public/cpp/system_tray_test_api.h"
#include "ash/shell.h"
#include "base/base_paths.h"
#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/metrics_hashes.h"
#include "base/metrics/statistics_recorder.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/threading/thread_restrictions.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "chrome/browser/ash/accessibility/accessibility_manager.h"
#include "chrome/browser/ash/accessibility/accessibility_test_utils.h"
#include "chrome/browser/ash/accessibility/dictation_bubble_test_helper.h"
#include "chrome/browser/ash/accessibility/speech_monitor.h"
#include "chrome/browser/ash/base/locale_util.h"
#include "chrome/browser/ash/input_method/textinput_test_helper.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/speech/speech_recognition_constants.h"
#include "chrome/browser/speech/speech_recognition_test_helper.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/metrics/content/subprocess_metrics_provider.h"
#include "components/prefs/pref_service.h"
#include "components/soda/soda_installer.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/accessibility_notification_waiter.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/fake_speech_recognition_manager.h"
#include "extensions/browser/browsertest_util.h"
#include "extensions/browser/extension_host_test_helper.h"
#include "media/mojo/mojom/speech_recognition_service.mojom.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/accessibility/accessibility_features.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/clipboard_monitor.h"
#include "ui/base/clipboard/clipboard_observer.h"
#include "ui/base/ime/ash/ime_bridge.h"
#include "ui/base/ime/ash/mock_ime_input_context_handler.h"
#include "ui/base/ime/fake_text_input_client.h"
#include "ui/base/ime/input_method_base.h"
#include "ui/events/test/event_generator.h"

namespace ash {
namespace {

constexpr int kPrintErrorMessageDelayMs = 3500;

const char kFirstSpeechResult[] = "Help";
const char16_t kFirstSpeechResult16[] = u"Help";
const char kFinalSpeechResult[] = "Hello world";
const char16_t kFinalSpeechResult16[] = u"Hello world";
const char16_t kTrySaying[] = u"Try saying:";
const char16_t kType[] = u"\"Type [word / phrase]\"";
const char16_t kHelp[] = u"\"Help\"";
const char16_t kUndo[] = u"\"Undo\"";
const char16_t kDelete[] = u"\"Delete\"";
const char16_t kSelectAll[] = u"\"Select all\"";
const char16_t kUnselect[] = u"\"Unselect\"";
const char16_t kCopy[] = u"\"Copy\"";
const char* kOnDeviceListeningDurationMetric =
    "Accessibility.CrosDictation.ListeningDuration.OnDeviceRecognition";
const char* kNetworkListeningDurationMetric =
    "Accessibility.CrosDictation.ListeningDuration.NetworkRecognition";
const char* kLocaleMetric = "Accessibility.CrosDictation.Language";
const char* kOnDeviceSpeechMetric =
    "Accessibility.CrosDictation.UsedOnDeviceSpeech";
const char* kPumpkinMetric = "Accessibility.CrosDictation.UsedPumpkin";
const char* kMacroRecognizedMetric =
    "Accessibility.CrosDictation.MacroRecognized";
const char* kMacroSucceededMetric =
    "Accessibility.CrosDictation.MacroSucceeded";
const char* kMacroFailedMetric = "Accessibility.CrosDictation.MacroFailed";
const int kInputTextViewMetricValue = 1;
constexpr char kPumpkinTestFilePath[] =
    "resources/chromeos/accessibility/accessibility_common/dictation/parse/"
    "pumpkin";
constexpr char kContentEditableUrl[] =
    "data:text/html;charset=utf-8,<div id='input' contenteditable></div>";
constexpr char kInputUrl[] =
    "data:text/html;charset=utf-8,<input id='input' type='text'></input>";
constexpr char kTextAreaUrl[] =
    "data:text/html;charset=utf-8,<textarea id='input'></textarea>";

static const char* kEnglishDictationCommands[] = {
    "delete",
    "move to the previous character",
    "move to the next character",
    "move to the previous line",
    "move to the next line",
    "copy",
    "paste",
    "cut",
    "undo",
    "redo",
    "select all",
    "unselect",
    "help",
    "new line",
    "cancel",
    "delete the previous word",
    "delete the previous sentence",
    "move to the next word",
    "move to the previous word",
    "delete phrase",
    "replace phrase with another phrase",
    "insert phrase before another phrase",
    "select from phrase to another phrase",
    "move to the next sentence",
    "move to the previous sentence"};

PrefService* GetActiveUserPrefs() {
  return ProfileManager::GetActiveUserProfile()->GetPrefs();
}

AccessibilityManager* GetManager() {
  return AccessibilityManager::Get();
}

void EnableChromeVox() {
  GetManager()->EnableSpokenFeedback(true);
}

std::string ToString(DictationBubbleIconType icon) {
  switch (icon) {
    case DictationBubbleIconType::kHidden:
      return "hidden";
    case DictationBubbleIconType::kStandby:
      return "standby";
    case DictationBubbleIconType::kMacroSuccess:
      return "macro success";
    case DictationBubbleIconType::kMacroFail:
      return "macro fail";
  }
}

// The type of editable field to use in tests.
enum class EditableType { kContentEditable, kInput, kTextArea };

// A class used to define the parameters of a test case.
class TestConfig {
 public:
  TestConfig(speech::SpeechRecognitionType speech_recognition_type,
             EditableType editable_type)
      : speech_recognition_type_(speech_recognition_type),
        editable_type_(editable_type) {}

  speech::SpeechRecognitionType speech_recognition_type() const {
    return speech_recognition_type_;
  }

  EditableType editable_type() const { return editable_type_; }

 private:
  speech::SpeechRecognitionType speech_recognition_type_;
  EditableType editable_type_;
};

// Listens for changes to the histogram provided at construction. This class
// only allows `Wait()` to be called once. If you need to call `Wait()` multiple
// times, create multiple instances of this class.
class HistogramWaiter {
 public:
  explicit HistogramWaiter(const char* metric_name) {
    histogram_observer_ = std::make_unique<
        base::StatisticsRecorder::ScopedHistogramSampleObserver>(
        metric_name, base::BindRepeating(&HistogramWaiter::OnHistogramCallback,
                                         base::Unretained(this)));
  }
  ~HistogramWaiter() { histogram_observer_.reset(); }

  HistogramWaiter(const HistogramWaiter&) = delete;
  HistogramWaiter& operator=(const HistogramWaiter&) = delete;

  // Waits for the next update to the observed histogram.
  void Wait() { run_loop_.Run(); }

  void OnHistogramCallback(const char* metric_name,
                           uint64_t name_hash,
                           base::HistogramBase::Sample sample) {
    run_loop_.Quit();
    histogram_observer_.reset();
  }

 private:
  std::unique_ptr<base::StatisticsRecorder::ScopedHistogramSampleObserver>
      histogram_observer_;
  base::RunLoop run_loop_;
};

// A class that repeatedly runs a function, which is supplied at construction,
// until it evaluates to true.
class SuccessWaiter {
 public:
  SuccessWaiter(const base::RepeatingCallback<bool()>& is_success,
                const std::string& error_message)
      : is_success_(is_success), error_message_(error_message) {}
  ~SuccessWaiter() = default;
  SuccessWaiter(const SuccessWaiter&) = delete;
  SuccessWaiter& operator=(const SuccessWaiter&) = delete;

  void Wait() {
    timer_.Start(FROM_HERE, base::Milliseconds(200), this,
                 &SuccessWaiter::OnTimer);
    content::GetUIThreadTaskRunner({})->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&SuccessWaiter::MaybePrintErrorMessage,
                       weak_factory_.GetWeakPtr()),
        base::Milliseconds(kPrintErrorMessageDelayMs));
    run_loop_.Run();
    ASSERT_TRUE(is_success_.Run());
  }

  void OnTimer() {
    if (is_success_.Run()) {
      timer_.Stop();
      run_loop_.Quit();
    }
  }

  void MaybePrintErrorMessage() {
    if (!timer_.IsRunning() || run_loop_.AnyQuitCalled() || is_success_.Run())
      return;

    LOG(ERROR) << "Still waiting for SuccessWaiter\n"
               << "SuccessWaiter error message: " << error_message_ << "\n";
  }

 private:
  base::RepeatingCallback<bool()> is_success_;
  std::string error_message_;
  base::RepeatingTimer timer_;
  base::RunLoop run_loop_;
  base::WeakPtrFactory<SuccessWaiter> weak_factory_{this};
};

class CaretBoundsChangedWaiter : public ui::InputMethodObserver {
 public:
  explicit CaretBoundsChangedWaiter(ui::InputMethod* input_method)
      : input_method_(input_method) {
    input_method_->AddObserver(this);
  }
  CaretBoundsChangedWaiter(const CaretBoundsChangedWaiter&) = delete;
  CaretBoundsChangedWaiter& operator=(const CaretBoundsChangedWaiter&) = delete;
  ~CaretBoundsChangedWaiter() override { input_method_->RemoveObserver(this); }

  void Wait() { run_loop_.Run(); }

 private:
  // ui::InputMethodObserver:
  void OnFocus() override {}
  void OnBlur() override {}
  void OnTextInputStateChanged(const ui::TextInputClient* client) override {}
  void OnInputMethodDestroyed(const ui::InputMethod* input_method) override {}
  void OnCaretBoundsChanged(const ui::TextInputClient* client) override {
    run_loop_.Quit();
  }

  ui::InputMethod* input_method_;
  base::RunLoop run_loop_;
};

// Listens for changes to the clipboard. This class only allows `Wait()` to be
// called once. If you need to call `Wait()` multiple times, create multiple
// instances of this class.
class ClipboardChangedWaiter : public ui::ClipboardObserver {
 public:
  ClipboardChangedWaiter() {
    ui::ClipboardMonitor::GetInstance()->AddObserver(this);
  }
  ClipboardChangedWaiter(const ClipboardChangedWaiter&) = delete;
  ClipboardChangedWaiter& operator=(const ClipboardChangedWaiter&) = delete;
  ~ClipboardChangedWaiter() override {
    ui::ClipboardMonitor::GetInstance()->RemoveObserver(this);
  }

  void Wait() { run_loop_.Run(); }

 private:
  // ui::ClipboardObserver:
  void OnClipboardDataChanged() override { run_loop_.Quit(); }

  base::RunLoop run_loop_;
};

}  // namespace

class DictationTestBase : public InProcessBrowserTest,
                          public ::testing::WithParamInterface<TestConfig> {
 public:
  DictationTestBase() : test_helper_(speech_recognition_type()) {}
  ~DictationTestBase() override = default;
  DictationTestBase(const DictationTestBase&) = delete;
  DictationTestBase& operator=(const DictationTestBase&) = delete;

 protected:
  // InProcessBrowserTest:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    std::vector<base::test::FeatureRef> enabled_features =
        test_helper_.GetEnabledFeatures();
    std::vector<base::test::FeatureRef> disabled_features =
        test_helper_.GetDisabledFeatures();
    scoped_feature_list_.InitWithFeatures(enabled_features, disabled_features);

    InProcessBrowserTest::SetUpCommandLine(command_line);
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    test_helper_.SetUp(browser()->profile());

    ASSERT_FALSE(AccessibilityManager::Get()->IsDictationEnabled());
    console_observer_ = std::make_unique<ExtensionConsoleErrorObserver>(
        browser()->profile(), extension_misc::kAccessibilityCommonExtensionId);
    browser()->profile()->GetPrefs()->SetBoolean(
        ash::prefs::kDictationAcceleratorDialogHasBeenAccepted, true);

    extensions::ExtensionHostTestHelper host_helper(
        browser()->profile(), extension_misc::kAccessibilityCommonExtensionId);
    AccessibilityManager::Get()->SetDictationEnabled(true);
    host_helper.WaitForHostCompletedFirstLoad();

    aura::Window* root_window = Shell::Get()->GetPrimaryRootWindow();
    generator_ = std::make_unique<ui::test::EventGenerator>(root_window);

    std::string url;
    switch (editable_type()) {
      case EditableType::kTextArea:
        url = kTextAreaUrl;
        break;
      case EditableType::kInput:
        url = kInputUrl;
        break;
      case EditableType::kContentEditable:
        url = kContentEditableUrl;
        break;
    }
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(url)));
    // Put focus in the text box.
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
        nullptr, ui::KeyboardCode::VKEY_TAB, false, false, false, false)));

    // Increase Dictation's NO_FOCUSED_IME timeout to reduce flakiness on slower
    // builds.
    std::string script = R"(
      accessibilityCommon.dictation_.increaseNoFocusedImeTimeoutForTesting_();
      window.domAutomationController.send("done");
    )";
    ExecuteAccessibilityCommonScript(script);
  }

  void TearDownOnMainThread() override {
    if (speech_recognition_type() == speech::SpeechRecognitionType::kNetwork)
      content::SpeechRecognitionManager::SetManagerForTesting(nullptr);

    InProcessBrowserTest::TearDownOnMainThread();
  }

  // Routers to SpeechRecognitionTestHelper methods.
  void WaitForRecognitionStarted() {
    test_helper_.WaitForRecognitionStarted();
    // Dictation initializes FocusHandler when speech recognition starts.
    // Several tests require FocusHandler logic, so wait for it to initialize
    // before proceeding.
    WaitForFocusHandler();
  }

  void WaitForRecognitionStopped() { test_helper_.WaitForRecognitionStopped(); }

  void SendInterimResultAndWait(const std::string& transcript) {
    test_helper_.SendInterimResultAndWait(transcript);
  }

  void SendFinalResultAndWait(const std::string& transcript) {
    test_helper_.SendFinalResultAndWait(transcript);
  }

  void SendErrorAndWait() { test_helper_.SendErrorAndWait(); }

  void SendFinalResultAndWaitForEditableValue(const std::string& result,
                                              const std::string& value) {
    // Ensure that the accessibility tree and the text area value are updated.
    content::AccessibilityNotificationWaiter waiter(
        browser()->tab_strip_model()->GetActiveWebContents(),
        ui::kAXModeComplete, ax::mojom::Event::kValueChanged);
    SendFinalResultAndWait(result);
    // TODO(https://crbug.com/1333354): Investigate why this does not always
    // return true.
    ASSERT_TRUE(waiter.WaitForNotification());
    WaitForEditableValue(value);
  }

  void SendFinalResultAndWaitForSelectionChanged(const std::string& result) {
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    content::AccessibilityNotificationWaiter selection_waiter(
        browser()->tab_strip_model()->GetActiveWebContents(),
        ui::kAXModeComplete,
        ui::AXEventGenerator::Event::TEXT_SELECTION_CHANGED);
    content::BoundingBoxUpdateWaiter bounding_box_waiter(web_contents);
    SendFinalResultAndWait(result);
    bounding_box_waiter.Wait();
    // TODO(https://crbug.com/1333354): Investigate why this does not always
    // return true.
    ASSERT_TRUE(selection_waiter.WaitForNotification());
  }

  void SendFinalResultAndWaitForCaretBoundsChanged(const std::string& result) {
    content::AccessibilityNotificationWaiter selection_waiter(
        browser()->tab_strip_model()->GetActiveWebContents(),
        ui::kAXModeComplete,
        ui::AXEventGenerator::Event::TEXT_SELECTION_CHANGED);
    CaretBoundsChangedWaiter caret_waiter(
        browser()->window()->GetNativeWindow()->GetHost()->GetInputMethod());
    SendFinalResultAndWait(result);
    caret_waiter.Wait();
    // TODO(https://crbug.com/1333354): Investigate why this does not always
    // return true.
    ASSERT_TRUE(selection_waiter.WaitForNotification());
  }

  void SendFinalResultAndWaitForClipboardChanged(const std::string& result) {
    ClipboardChangedWaiter waiter;
    SendFinalResultAndWait(result);
    waiter.Wait();
  }

  std::string GetEditableValue() {
    std::string output;
    std::string script;
    switch (editable_type()) {
      case EditableType::kTextArea:
      case EditableType::kInput:
        script =
            "window.domAutomationController.send("
            "document.getElementById('input').value)";
        break;
      case EditableType::kContentEditable:
        script =
            "window.domAutomationController.send("
            "document.getElementById('input').innerText)";
        break;
    }
    CHECK(ExecuteScriptAndExtractString(
        browser()->tab_strip_model()->GetWebContentsAt(0), script, &output));
    return output;
  }

  void WaitForEditableValue(const std::string& value) {
    std::string error_message = "Still waiting for text area value: " + value;
    SuccessWaiter(base::BindLambdaForTesting(
                      [&]() { return value == GetEditableValue(); }),
                  error_message)
        .Wait();
  }

  void WaitForFocusHandler() {
    std::string error_message = "Still waiting for FocusHandler";
    std::string script = R"(
      if (accessibilityCommon.dictation_.focusHandler_.isReadyForTesting()) {
        window.domAutomationController.send("ready");
      } else {
        window.domAutomationController.send("not ready");
      }
    )";
    SuccessWaiter(
        base::BindLambdaForTesting([&]() {
          std::string result =
              extensions::browsertest_util::ExecuteScriptInBackgroundPage(
                  /*context=*/browser()->profile(),
                  /*extension_id=*/
                  extension_misc::kAccessibilityCommonExtensionId,
                  /*script=*/script);
          return result == "ready";
        }),
        error_message)
        .Wait();
  }

  void ToggleDictationWithKeystroke() {
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
        nullptr, ui::KeyboardCode::VKEY_D, false, false, false, true)));
  }

  void InstallMockInputContextHandler() {
    input_context_handler_ = std::make_unique<ui::MockIMEInputContextHandler>();
    ui::IMEBridge::Get()->SetInputContextHandler(input_context_handler_.get());
  }

  // Retrieves the number of times commit text is updated.
  int GetCommitTextCallCount() {
    EXPECT_TRUE(input_context_handler_);
    return input_context_handler_->commit_text_call_count();
  }

  void WaitForCommitText(const std::u16string& value) {
    std::string error_message =
        base::UTF16ToUTF8(u"Still waiting for commit text: " + value);
    EXPECT_TRUE(input_context_handler_);
    SuccessWaiter(base::BindLambdaForTesting([&]() {
                    return value == input_context_handler_->last_commit_text();
                  }),
                  error_message)
        .Wait();
  }

  const base::flat_map<std::string, Dictation::LocaleData>
  GetAllSupportedLocales() {
    return Dictation::GetAllSupportedLocales();
  }

  std::string ExecuteAccessibilityCommonScript(const std::string& script) {
    return extensions::browsertest_util::ExecuteScriptInBackgroundPage(
        /*context=*/browser()->profile(),
        /*extension_id=*/extension_misc::kAccessibilityCommonExtensionId,
        /*script=*/script);
  }

  std::string GetClipboardText() {
    std::u16string text;
    ui::Clipboard::GetForCurrentThread()->ReadText(
        ui::ClipboardBuffer::kCopyPaste, /*data_dst=*/nullptr, &text);
    return base::UTF16ToUTF8(text);
  }

  bool RunOnMultilineContent() {
    // <input> represents a one-line plain text control, so multiline test cases
    // should be skipped.
    return editable_type() != EditableType::kInput;
  }

  speech::SpeechRecognitionType speech_recognition_type() {
    return GetParam().speech_recognition_type();
  }

  EditableType editable_type() { return GetParam().editable_type(); }

 private:
  SpeechRecognitionTestHelper test_helper_;
  base::test::ScopedFeatureList scoped_feature_list_;
  std::unique_ptr<ui::MockIMEInputContextHandler> input_context_handler_;
  std::unique_ptr<ui::test::EventGenerator> generator_;
  std::unique_ptr<ExtensionConsoleErrorObserver> console_observer_;
};

class DictationTest : public DictationTestBase {
 public:
  DictationTest() = default;
  ~DictationTest() override = default;
  DictationTest(const DictationTest&) = delete;
  DictationTest& operator=(const DictationTest&) = delete;

 protected:
  void SetUpOnMainThread() override {
    GetActiveUserPrefs()->SetString(prefs::kAccessibilityDictationLocale,
                                    "en-US");
    DictationTestBase::SetUpOnMainThread();
  }
};

INSTANTIATE_TEST_SUITE_P(
    NetworkTextArea,
    DictationTest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kNetwork,
                                 EditableType::kTextArea)));

INSTANTIATE_TEST_SUITE_P(
    NetworkInput,
    DictationTest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kNetwork,
                                 EditableType::kInput)));

INSTANTIATE_TEST_SUITE_P(
    NetworkContentEditable,
    DictationTest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kNetwork,
                                 EditableType::kContentEditable)));

INSTANTIATE_TEST_SUITE_P(
    OnDeviceTextArea,
    DictationTest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kOnDevice,
                                 EditableType::kTextArea)));

// Tests the behavior of the GetAllSupportedLocales method, specifically how
// it sets locale data.
IN_PROC_BROWSER_TEST_P(DictationTest, GetAllSupportedLocales) {
  auto locales = GetAllSupportedLocales();
  for (auto& it : locales) {
    const std::string locale = it.first;
    bool works_offline = it.second.works_offline;
    bool installed = it.second.installed;
    if (speech_recognition_type() == speech::SpeechRecognitionType::kOnDevice &&
        locale == speech::kUsEnglishLocale) {
      // Currently, the only locale supported by SODA is en-US. It should work
      // offline and be installed.
      EXPECT_TRUE(works_offline);
      EXPECT_TRUE(installed);
    } else {
      EXPECT_FALSE(works_offline);
      EXPECT_FALSE(installed);
    }
  }

  if (speech_recognition_type() == speech::SpeechRecognitionType::kOnDevice) {
    // Uninstall SODA and all language packs.
    speech::SodaInstaller::GetInstance()->UninstallSodaForTesting();
  } else {
    return;
  }

  locales = GetAllSupportedLocales();
  for (auto& it : locales) {
    const std::string locale = it.first;
    bool works_offline = it.second.works_offline;
    bool installed = it.second.installed;
    if (locale == speech::kUsEnglishLocale) {
      // en-US should be marked as "works offline", but it shouldn't be
      // installed.
      EXPECT_TRUE(works_offline);
      EXPECT_FALSE(installed);
    } else {
      EXPECT_FALSE(works_offline);
      EXPECT_FALSE(installed);
    }
  }
}

IN_PROC_BROWSER_TEST_P(DictationTest, StartsAndStopsRecognition) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
}

IN_PROC_BROWSER_TEST_P(DictationTest, EntersFinalizedSpeech) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  SendFinalResultAndWaitForEditableValue(kFinalSpeechResult,
                                         kFinalSpeechResult);
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
}

// Tests that multiple finalized strings can be committed to the text area.
// Also ensures that spaces are added between finalized utterances.
IN_PROC_BROWSER_TEST_P(DictationTest, EntersMultipleFinalizedStrings) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  SendFinalResultAndWaitForEditableValue("The rain in Spain",
                                         "The rain in Spain");
  SendFinalResultAndWaitForEditableValue(
      "falls mainly on the plain.",
      "The rain in Spain falls mainly on the plain.");
  SendFinalResultAndWaitForEditableValue(
      "Vega is a star.",
      "The rain in Spain falls mainly on the plain. Vega is a star.");
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
}

IN_PROC_BROWSER_TEST_P(DictationTest, OnlyAddSpaceWhenNecessary) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  SendFinalResultAndWaitForEditableValue("The rain in Spain",
                                         "The rain in Spain");
  // Artificially add a space to this utterance. Verify that only one space is
  // added.
  SendFinalResultAndWaitForEditableValue(
      " falls mainly on the plain.",
      "The rain in Spain falls mainly on the plain.");
  // Artificially add a space to this utterance. Verify that only one space is
  // added.
  SendFinalResultAndWaitForEditableValue(
      " Vega is a star.",
      "The rain in Spain falls mainly on the plain. Vega is a star.");
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
}

IN_PROC_BROWSER_TEST_P(DictationTest, RecognitionEndsWhenInputFieldLosesFocus) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  SendFinalResultAndWaitForEditableValue("Vega is a star", "Vega is a star");
  ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      nullptr, ui::KeyboardCode::VKEY_TAB, false, false, false, false)));
  WaitForRecognitionStopped();
  EXPECT_EQ("Vega is a star", GetEditableValue());
}

// TODO(crbug.com/1352312): Flaky.
IN_PROC_BROWSER_TEST_P(DictationTest,
                       DISABLED_UserEndsDictationWhenChromeVoxEnabled) {
  EnableChromeVox();
  EXPECT_TRUE(GetManager()->IsSpokenFeedbackEnabled());
  InstallMockInputContextHandler();

  GetManager()->ToggleDictation();
  WaitForRecognitionStarted();
  SendInterimResultAndWait(kFinalSpeechResult);
  GetManager()->ToggleDictation();
  WaitForRecognitionStopped();

  WaitForCommitText(kFinalSpeechResult16);
}

IN_PROC_BROWSER_TEST_P(DictationTest, ChromeVoxSilencedWhenToggledOn) {
  // Set up ChromeVox.
  test::SpeechMonitor sm;
  EXPECT_FALSE(GetManager()->IsSpokenFeedbackEnabled());
  extensions::ExtensionHostTestHelper host_helper(
      browser()->profile(), extension_misc::kChromeVoxExtensionId);
  EnableChromeVox();
  host_helper.WaitForHostCompletedFirstLoad();
  EXPECT_TRUE(GetManager()->IsSpokenFeedbackEnabled());

  // Not yet forced to stop.
  EXPECT_EQ(0, sm.stop_count());

  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();

  // Assert ChromeVox was asked to stop speaking at the toggle.
  EXPECT_EQ(1, sm.stop_count());
}

IN_PROC_BROWSER_TEST_P(DictationTest, EntersInterimSpeechWhenToggledOff) {
  InstallMockInputContextHandler();

  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  SendInterimResultAndWait(kFirstSpeechResult);
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
  WaitForCommitText(kFirstSpeechResult16);
}

// Tests that commit text is not updated if the user toggles dictation and no
// speech results are processed.
IN_PROC_BROWSER_TEST_P(DictationTest, UserEndsDictationBeforeSpeech) {
  InstallMockInputContextHandler();
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
  EXPECT_EQ(0, GetCommitTextCallCount());
}

// Ensures that the correct metrics are recorded when Dictation is toggled.
IN_PROC_BROWSER_TEST_P(DictationTest, Metrics) {
  base::HistogramTester histogram_tester_;
  bool on_device =
      speech_recognition_type() == speech::SpeechRecognitionType::kOnDevice;
  const char* metric_name = on_device ? kOnDeviceListeningDurationMetric
                                      : kNetworkListeningDurationMetric;
  HistogramWaiter waiter(metric_name);
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
  waiter.Wait();
  content::FetchHistogramsFromChildProcesses();
  metrics::SubprocessMetricsProvider::MergeHistogramDeltasForTesting();

  // Ensure that we recorded the correct locale.
  histogram_tester_.ExpectUniqueSample(/*name=*/kLocaleMetric,
                                       /*sample=*/base::HashMetricName("en-US"),
                                       /*expected_bucket_count=*/1);
  // Ensure that we recorded the type of speech recognition and listening
  // duration.
  if (on_device) {
    histogram_tester_.ExpectUniqueSample(/*name=*/kOnDeviceSpeechMetric,
                                         /*sample=*/true,
                                         /*expected_bucket_count=*/1);
    ASSERT_EQ(1u,
              histogram_tester_.GetAllSamples(kOnDeviceListeningDurationMetric)
                  .size());
    // Ensure there are no metrics for the other type of speech recognition.
    ASSERT_EQ(0u,
              histogram_tester_.GetAllSamples(kNetworkListeningDurationMetric)
                  .size());
  } else {
    histogram_tester_.ExpectUniqueSample(/*name=*/kOnDeviceSpeechMetric,
                                         /*sample=*/false,
                                         /*expected_bucket_count=*/1);
    ASSERT_EQ(1u,
              histogram_tester_.GetAllSamples(kNetworkListeningDurationMetric)
                  .size());
    // Ensure there are no metrics for the other type of speech recognition.
    ASSERT_EQ(0u,
              histogram_tester_.GetAllSamples(kOnDeviceListeningDurationMetric)
                  .size());
  }
}

IN_PROC_BROWSER_TEST_P(DictationTest,
                       DictationStopsWhenSystemTrayBecomesVisible) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  SystemTrayTestApi::Create()->ShowBubble();
  WaitForRecognitionStopped();
}

IN_PROC_BROWSER_TEST_P(DictationTest, NoExtraSpaceForPunctuation) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  SendFinalResultAndWaitForEditableValue("Hello world", "Hello world");
  SendFinalResultAndWaitForEditableValue(".", "Hello world.");
  SendFinalResultAndWaitForEditableValue("Goodnight", "Hello world. Goodnight");
  SendFinalResultAndWaitForEditableValue("!", "Hello world. Goodnight!");
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
}

IN_PROC_BROWSER_TEST_P(DictationTest, StopListening) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  SendFinalResultAndWait("cancel");
  WaitForRecognitionStopped();
}

IN_PROC_BROWSER_TEST_P(DictationTest, SmartCapitalization) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  SendFinalResultAndWaitForEditableValue("This", "This");
  SendFinalResultAndWaitForEditableValue("Is", "This is");
  SendFinalResultAndWaitForEditableValue("a test.", "This is a test.");
  SendFinalResultAndWaitForEditableValue("you passed!",
                                         "This is a test. You passed!");
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
}

IN_PROC_BROWSER_TEST_P(DictationTest, SmartCapitalizationWithComma) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  SendFinalResultAndWaitForEditableValue("Hello,", "Hello,");
  SendFinalResultAndWaitForEditableValue("world", "Hello, world");
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
}

// Note: this test runs the SMART_DELETE_PHRASE macro and at first glance
// should be categorized as a DictationCommandsTest. However, this test stops
// speech recognition in the middle of the test, which directly conflicts with
// DictationCommandsTest's behavior to automatically stop speech recognition
// during teardown. Thus we need this to be a DictationTest so that we don't
// try to stop speech recognition when it's already been stopped.
IN_PROC_BROWSER_TEST_P(DictationTest, SmartDeletePhraseNoChange) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  SendFinalResultAndWaitForEditableValue("Hello world", "Hello world");
  SendFinalResultAndWait("delete banana");
  SendFinalResultAndWait("cancel");
  WaitForRecognitionStopped();
  // The text area value isn't changed because Dictation parses 'delete banana'
  // as the SMART_DELETE_PHRASE macro. Since there is no instance of 'banana' in
  // the text field, the macro does nothing and the text area value remains
  // unchanged. In the future, we can verify that context-checking failed and an
  // error was surfaced to the user.
  ASSERT_EQ("Hello world", GetEditableValue());
}

class DictationWithAutoclickTest : public DictationTestBase {
 public:
  DictationWithAutoclickTest() = default;
  ~DictationWithAutoclickTest() override = default;
  DictationWithAutoclickTest(const DictationWithAutoclickTest&) = delete;
  DictationWithAutoclickTest& operator=(const DictationWithAutoclickTest&) =
      delete;

 protected:
  void SetUpOnMainThread() override {
    // Setup Autoclick first, then setup Dictation. This is to ensure that
    // Autoclick doesn't steal focus away from the textarea (either by clicking
    // or via the presence of the Autoclick UI, which steals focus when
    // initially shown).
    GetActiveUserPrefs()->SetInteger(prefs::kAccessibilityAutoclickDelayMs,
                                     90 * 1000);
    GetActiveUserPrefs()->CommitPendingWrite();
    GetManager()->EnableAutoclick(true);
    EXPECT_TRUE(GetManager()->IsAutoclickEnabled());

    DictationTestBase::SetUpOnMainThread();
  }
};

INSTANTIATE_TEST_SUITE_P(
    NetworkTextArea,
    DictationWithAutoclickTest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kNetwork,
                                 EditableType::kTextArea)));

INSTANTIATE_TEST_SUITE_P(
    OnDeviceTextArea,
    DictationWithAutoclickTest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kOnDevice,
                                 EditableType::kTextArea)));

IN_PROC_BROWSER_TEST_P(DictationWithAutoclickTest, CanDictate) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  SendFinalResultAndWaitForEditableValue("Hello world", "Hello world");
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
}

// Tests the behavior of Dictation in Japanese.
class DictationJaTest : public DictationTestBase {
 public:
  DictationJaTest() = default;
  ~DictationJaTest() override = default;
  DictationJaTest(const DictationJaTest&) = delete;
  DictationJaTest& operator=(const DictationJaTest&) = delete;

 protected:
  void SetUpOnMainThread() override {
    locale_util::SwitchLanguage("ja", /*enable_locale_keyboard_layouts=*/true,
                                /*login_layouts_only*/ false, base::DoNothing(),
                                browser()->profile());
    g_browser_process->SetApplicationLocale("ja");
    GetActiveUserPrefs()->SetString(prefs::kAccessibilityDictationLocale, "ja");

    DictationTestBase::SetUpOnMainThread();
  }
};

// On-device speech recognition is currently limited to en-US, so
// DictationJaTest should use network speech recognition only.
INSTANTIATE_TEST_SUITE_P(
    NetworkTextArea,
    DictationJaTest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kNetwork,
                                 EditableType::kTextArea)));

INSTANTIATE_TEST_SUITE_P(
    NetworkInput,
    DictationJaTest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kNetwork,
                                 EditableType::kInput)));

IN_PROC_BROWSER_TEST_P(DictationJaTest, NoSmartSpacingOrCapitalization) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  SendFinalResultAndWaitForEditableValue("this", "this");
  SendFinalResultAndWaitForEditableValue(" Is", "this Is");
  SendFinalResultAndWaitForEditableValue("a test.", "this Isa test.");
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
}

IN_PROC_BROWSER_TEST_P(DictationJaTest, CanDictate) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  SendFinalResultAndWaitForEditableValue("テニス", "テニス");
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
}

IN_PROC_BROWSER_TEST_P(DictationJaTest, DeleteCharacter) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  // Dictate "tennis".
  SendFinalResultAndWaitForEditableValue("テニス", "テニス");
  // Perform the 'delete' command.
  SendFinalResultAndWaitForEditableValue("削除", "テニ");
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
}

IN_PROC_BROWSER_TEST_P(DictationJaTest, SmartDeletePhrase) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  // Dictate "I like basketball".
  SendFinalResultAndWaitForEditableValue("私はバスケットボールが好きです。",
                                         "私はバスケットボールが好きです。");
  // Delete "I" e.g. the first two characters in the sentence.
  SendFinalResultAndWaitForEditableValue("私はを削除",
                                         "バスケットボールが好きです。");
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
}

IN_PROC_BROWSER_TEST_P(DictationJaTest, SmartReplacePhrase) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  // Dictate "I like basketball".
  SendFinalResultAndWaitForEditableValue("私はバスケットボールが好きです。",
                                         "私はバスケットボールが好きです。");
  // Replace "basketball" with "tennis".
  SendFinalResultAndWaitForEditableValue("バスケットボールをテニスに置き換え",
                                         "私はテニスが好きです。");
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
}

IN_PROC_BROWSER_TEST_P(DictationJaTest, SmartInsertBefore) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  // Dictate "I like tennis".
  SendFinalResultAndWaitForEditableValue("私はテニスが好きです。",
                                         "私はテニスが好きです。");
  // Insert "basketball and" before "tennis".
  // Final text area value should be "I like basketball and tennis".
  SendFinalResultAndWaitForEditableValue(
      "バスケットボールとをテニスの前に挿入",
      "私はバスケットボールとテニスが好きです。");
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
}

IN_PROC_BROWSER_TEST_P(DictationJaTest, SmartSelectBetweenAndDictate) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  // Dictate "I like tennis".
  SendFinalResultAndWaitForEditableValue("私はテニスが好きです。",
                                         "私はテニスが好きです。");
  // Select the entire text using the SMART_SELECT_BETWEEN command.
  SendFinalResultAndWaitForSelectionChanged("私はから好きですまで選択");
  // Dictate "congratulations", which should replace the selected text.
  SendFinalResultAndWaitForEditableValue("おめでとう", "おめでとう。");
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
}

IN_PROC_BROWSER_TEST_P(DictationJaTest, SmartSelectBetweenAndDelete) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  // Dictate "I like tennis".
  SendFinalResultAndWaitForEditableValue("私はテニスが好きです。",
                                         "私はテニスが好きです。");
  // Select between "I" and "tennis" using the SMART_SELECT_BETWEEN command
  // (roughly the first half of the text).
  SendFinalResultAndWaitForSelectionChanged("私はからテニスまで選択");
  // Perform the delete command.
  SendFinalResultAndWaitForEditableValue("削除", "が好きです。");
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
}

class DictationCommandsTest : public DictationTest {
 protected:
  DictationCommandsTest() {}
  ~DictationCommandsTest() override = default;
  DictationCommandsTest(const DictationCommandsTest&) = delete;
  DictationCommandsTest& operator=(const DictationCommandsTest&) = delete;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    DictationTest::SetUpCommandLine(command_line);
    scoped_feature_list_.InitAndEnableFeature(
        ::features::kExperimentalAccessibilityDictationMoreCommands);
  }

  void SetUpOnMainThread() override {
    DictationTest::SetUpOnMainThread();
    ToggleDictationWithKeystroke();
    WaitForRecognitionStarted();
  }

  void TearDownOnMainThread() override {
    ToggleDictationWithKeystroke();
    WaitForRecognitionStopped();
    DictationTest::TearDownOnMainThread();
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

INSTANTIATE_TEST_SUITE_P(
    NetworkTextArea,
    DictationCommandsTest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kNetwork,
                                 EditableType::kTextArea)));

INSTANTIATE_TEST_SUITE_P(
    NetworkInput,
    DictationCommandsTest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kNetwork,
                                 EditableType::kInput)));

INSTANTIATE_TEST_SUITE_P(
    OnDeviceTextArea,
    DictationCommandsTest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kOnDevice,
                                 EditableType::kTextArea)));

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, TypesCommands) {
  std::string expected_text = "";
  int i = 0;
  for (const char* command : kEnglishDictationCommands) {
    std::string type_command = "type ";
    if (i == 0) {
      expected_text += command;
      expected_text[0] = base::ToUpperASCII(expected_text[0]);
    } else {
      expected_text += " ";
      expected_text += command;
    }
    SendFinalResultAndWaitForEditableValue(type_command + command,
                                           expected_text);
    ++i;
  }
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, TypesNonCommands) {
  // The phrase should be entered without the word "type".
  SendFinalResultAndWaitForEditableValue("Type this is a test",
                                         "This is a test");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, DeleteCharacter) {
  SendFinalResultAndWaitForEditableValue("Vega", "Vega");
  // Capitalization and whitespace shouldn't matter.
  SendFinalResultAndWaitForEditableValue(" Delete", "Veg");
  SendFinalResultAndWaitForEditableValue("delete", "Ve");
  SendFinalResultAndWaitForEditableValue("  delete ", "V");
  SendFinalResultAndWaitForEditableValue("DELETE", "");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, MoveByCharacter) {
  SendFinalResultAndWaitForEditableValue("Lyra", "Lyra");
  SendFinalResultAndWaitForCaretBoundsChanged("Move to the Previous character");
  // White space is added to the text on the left of the text caret, but not
  // to the right of the text caret.
  SendFinalResultAndWaitForEditableValue("inserted", "Lyr inserted a");
  SendFinalResultAndWaitForCaretBoundsChanged("move TO the next character ");
  SendFinalResultAndWaitForEditableValue("is a constellation",
                                         "Lyr inserted a is a constellation");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, NewLineAndMoveByLine) {
  if (!RunOnMultilineContent())
    return;

  SendFinalResultAndWaitForEditableValue("Line 1", "Line 1");
  SendFinalResultAndWaitForEditableValue("new line", "Line 1\n");
  SendFinalResultAndWaitForEditableValue("line 2", "Line 1\nline 2");
  SendFinalResultAndWaitForCaretBoundsChanged("Move to the previous line ");
  SendFinalResultAndWaitForEditableValue("up", "Line 1 up\nline 2");
  SendFinalResultAndWaitForCaretBoundsChanged("Move to the next line");
  SendFinalResultAndWaitForEditableValue("down", "Line 1 up\nline 2 down");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, UndoAndRedo) {
  SendFinalResultAndWaitForEditableValue("The constellation",
                                         "The constellation");
  SendFinalResultAndWaitForEditableValue(" Myra", "The constellation Myra");
  SendFinalResultAndWaitForEditableValue("undo", "The constellation");
  SendFinalResultAndWaitForEditableValue(" Lyra", "The constellation Lyra");
  SendFinalResultAndWaitForEditableValue("undo", "The constellation");
  SendFinalResultAndWaitForEditableValue("redo", "The constellation Lyra");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, SelectAllAndUnselect) {
  SendFinalResultAndWaitForEditableValue("Vega is the brightest star in Lyra",
                                         "Vega is the brightest star in Lyra");
  SendFinalResultAndWaitForSelectionChanged("Select all");
  SendFinalResultAndWaitForEditableValue("delete", "");
  SendFinalResultAndWaitForEditableValue(
      "Vega is the fifth brightest star in the sky",
      "Vega is the fifth brightest star in the sky");
  SendFinalResultAndWaitForSelectionChanged("Select all");
  SendFinalResultAndWaitForSelectionChanged("Unselect");
  SendFinalResultAndWaitForEditableValue(
      "!", "Vega is the fifth brightest star in the sky!");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, CutCopyPaste) {
  SendFinalResultAndWaitForEditableValue("Star", "Star");
  SendFinalResultAndWaitForSelectionChanged("Select all");
  SendFinalResultAndWaitForClipboardChanged("Copy");
  EXPECT_EQ("Star", GetClipboardText());
  SendFinalResultAndWaitForSelectionChanged("unselect");
  SendFinalResultAndWaitForEditableValue("paste", "StarStar");
  SendFinalResultAndWaitForSelectionChanged("select ALL ");
  SendFinalResultAndWaitForClipboardChanged("cut");
  EXPECT_EQ("StarStar", GetClipboardText());
  WaitForEditableValue("");
  SendFinalResultAndWaitForEditableValue("  PaStE ", "StarStar");
}

// Ensures that a metric is recorded when a macro succeeds.
// TODO(crbug.com/1288964): Add a test to ensure that a metric is recorded when
// a macro fails.
IN_PROC_BROWSER_TEST_P(DictationCommandsTest, MacroSucceededMetric) {
  base::HistogramTester histogram_tester_;
  SendFinalResultAndWaitForEditableValue("Vega is the brightest star in Lyra",
                                         "Vega is the brightest star in Lyra");
  histogram_tester_.ExpectUniqueSample(/*name=*/kMacroSucceededMetric,
                                       /*sample=*/kInputTextViewMetricValue,
                                       /*expected_bucket_count=*/1);
  histogram_tester_.ExpectUniqueSample(/*name=*/kMacroFailedMetric,
                                       /*sample=*/kInputTextViewMetricValue,
                                       /*expected_bucket_count=*/0);
  histogram_tester_.ExpectUniqueSample(/*name=*/kMacroRecognizedMetric,
                                       /*sample=*/kInputTextViewMetricValue,
                                       /*expected_bucket_count=*/1);
}

// Flaky on Linux (crbug.com/1348608).
#if BUILDFLAG(IS_LINUX)
#define MAYBE_Help DISABLED_Help
#else
#define MAYBE_Help Help
#endif
IN_PROC_BROWSER_TEST_P(DictationCommandsTest, MAYBE_Help) {
  SendFinalResultAndWait("help");

  // Wait for the help URL to load.
  SuccessWaiter(
      base::BindLambdaForTesting([&]() {
        content::WebContents* web_contents =
            browser()->tab_strip_model()->GetActiveWebContents();
        return web_contents->GetVisibleURL() ==
               "https://support.google.com/chromebook?p=text_dictation_m100";
      }),
      "Still waiting for help URL to load")
      .Wait();

  // Opening a new tab with the help center article toggles Dictation off.
  WaitForRecognitionStopped();
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, DeletePrevWordSimple) {
  SendFinalResultAndWaitForEditableValue("This is a test", "This is a test");
  SendFinalResultAndWaitForEditableValue("delete the previous word",
                                         "This is a ");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, DeletePrevWordExtraSpace) {
  SendFinalResultAndWaitForEditableValue("This is a test ", "This is a test ");
  SendFinalResultAndWaitForEditableValue("delete the previous word",
                                         "This is a ");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, DeletePrevWordNewLine) {
  if (!RunOnMultilineContent())
    return;

  SendFinalResultAndWaitForEditableValue("This is a test\n\n",
                                         "This is a test\n\n");
  SendFinalResultAndWaitForEditableValue("delete the previous word",
                                         "This is a test\n");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, DeletePrevWordPunctuation) {
  SendFinalResultAndWaitForEditableValue("This.is.a.test. ",
                                         "This.is.a.test. ");
  SendFinalResultAndWaitForEditableValue("delete the previous word",
                                         "This.is.a.test");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, DeletePrevWordMiddleOfWord) {
  SendFinalResultAndWaitForEditableValue("This is a test.", "This is a test.");
  // Move the text caret into the middle of the word "test".
  SendFinalResultAndWaitForCaretBoundsChanged("Move to the Previous character");
  SendFinalResultAndWaitForCaretBoundsChanged("Move to the Previous character");
  SendFinalResultAndWaitForEditableValue("delete the previous word",
                                         "This is a t.");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, DeleteAllTextSimple) {
  SendFinalResultAndWaitForEditableValue("Hello, world.", "Hello, world.");
  SendFinalResultAndWaitForEditableValue("delete all", "");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, DeleteAllTextMultiLineString) {
  if (!RunOnMultilineContent())
    return;

  std::string text = " Hello, world. \n Hello, world. \n Hello, world. \n";
  SendFinalResultAndWaitForEditableValue(text, text);
  SendFinalResultAndWaitForEditableValue("delete all", "");
}

// TODO(crbug.com/1362842) Add a test where
// you disable the MoreCommands Feature Flag
// and ensure that you can't run the Dictation
// Commands under that feature flag.

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, DISABLED_NavStartTextSimple) {
  SendFinalResultAndWaitForEditableValue("Is good", "Is good");
  SendFinalResultAndWaitForCaretBoundsChanged("move to the start");
  SendFinalResultAndWaitForEditableValue("the weather outside",
                                         "the weather outside Is good");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest,
                       DISABLED_NavStartTextMultiLineString) {
  if (!RunOnMultilineContent())
    return;

  std::string text = "Is good\n and we should go for a run.";
  SendFinalResultAndWaitForEditableValue(text, text);
  SendFinalResultAndWaitForCaretBoundsChanged("move to the start");
  std::string expected = "the weather outside " + text;
  SendFinalResultAndWaitForEditableValue("the weather outside", expected);
}

#if BUILDFLAG(IS_CHROMEOS) && defined(MEMORY_SANITIZER)
#define MAYBE_NavEndTextSimple DISABLED_NavEndTextSimple
#else
#define MAYBE_NavEndTextSimple NavEndTextSimple
#endif
IN_PROC_BROWSER_TEST_P(DictationCommandsTest, MAYBE_NavEndTextSimple) {
  SendFinalResultAndWaitForEditableValue("The weather outside is",
                                         "The weather outside is");
  SendFinalResultAndWaitForCaretBoundsChanged("move to the start");
  SendFinalResultAndWaitForCaretBoundsChanged("move to the end");
  std::string expected = "The weather outside is good";
  SendFinalResultAndWaitForEditableValue("good", expected);
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, NavEndTextMultiLineString) {
  if (!RunOnMultilineContent())
    return;

  std::string text = "The weather outside is\n";
  SendFinalResultAndWaitForEditableValue(text, text);
  SendFinalResultAndWaitForCaretBoundsChanged("move to the start");
  SendFinalResultAndWaitForCaretBoundsChanged("move to the end");
  std::string expected = text + "good";
  SendFinalResultAndWaitForEditableValue("good", expected);
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, SelectPrevWordSimple) {
  SendFinalResultAndWaitForEditableValue("The weather today is bad",
                                         "The weather today is bad");
  SendFinalResultAndWaitForSelectionChanged("highlight the previous word");
  std::string expected = "The weather today is nice";
  SendFinalResultAndWaitForEditableValue("nice", expected);
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, SelectPrevWordNewLine) {
  if (!RunOnMultilineContent())
    return;

  std::string text = "The weather today is bad\n";
  SendFinalResultAndWaitForEditableValue(text, text);
  SendFinalResultAndWait("highlight the previous word");
  std::string expected = "The weather today is badnice";
  SendFinalResultAndWaitForEditableValue("nice", expected);
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, SelectNextWordSimple) {
  SendFinalResultAndWaitForEditableValue("The weather today is bad",
                                         "The weather today is bad");
  SendFinalResultAndWaitForCaretBoundsChanged("move to the previous word");
  SendFinalResultAndWaitForSelectionChanged("highlight the next word");
  std::string expected = "The weather today is nice";
  SendFinalResultAndWaitForEditableValue("nice", expected);
}

// TODO(crbug.com/1377314): Test is flaky.
IN_PROC_BROWSER_TEST_P(DictationCommandsTest, DISABLED_SelectNextWordNewLine) {
  std::string text = "The weather today is\n";
  SendFinalResultAndWaitForEditableValue(text, text);
  SendFinalResultAndWait("move to the previous character");
  SendFinalResultAndWait("highlight the next word");
  std::string expected = "The weather today isnice";
  SendFinalResultAndWaitForEditableValue("nice", expected);
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, SelectNextCharSimple) {
  SendFinalResultAndWaitForEditableValue("Text", "Text");
  SendFinalResultAndWaitForCaretBoundsChanged("move to the previous word");
  SendFinalResultAndWaitForCaretBoundsChanged("highlight the next character");
  std::string expected = "ext";
  SendFinalResultAndWaitForEditableValue("delete", expected);
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, SelectNextCharMultiLineString) {
  if (!RunOnMultilineContent())
    return;

  std::string text = "Hello, world.\n";
  SendFinalResultAndWaitForEditableValue(text, text);
  SendFinalResultAndWaitForCaretBoundsChanged("move to the previous word");
  SendFinalResultAndWait("highlight the next character");
  std::string expected = "Hello, world.";
  SendFinalResultAndWaitForEditableValue("delete", expected);
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, SelectPrevCharSimple) {
  SendFinalResultAndWaitForEditableValue("Text", "Text");
  SendFinalResultAndWaitForCaretBoundsChanged("highlight the previous character");
  std::string expected = "Tex";
  SendFinalResultAndWaitForEditableValue("delete", expected);
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, SelectPrevCharMultiLineString) {
  if (!RunOnMultilineContent())
    return;

  std::string text = "Hello, world.\n";
  SendFinalResultAndWaitForEditableValue(text, text);
  SendFinalResultAndWait("highlight the previous character");
  std::string expected = "Hello, world.";
  SendFinalResultAndWaitForEditableValue("delete", expected);
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, DeletePrevSentSimple) {
  SendFinalResultAndWaitForEditableValue("Hello, world.", "Hello, world.");
  SendFinalResultAndWaitForEditableValue("delete the previous sentence", "");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, DeletePrevSentWhiteSpace) {
  if (!RunOnMultilineContent())
    return;

  SendFinalResultAndWaitForEditableValue("  \nHello, world.\n  ",
                                         "  \nHello, world.\n  ");
  SendFinalResultAndWaitForEditableValue("delete the previous sentence", "");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, DeletePrevSentPunctuation) {
  SendFinalResultAndWaitForEditableValue(
      "Hello, world! Good afternoon; good evening? Goodnight, world.",
      "Hello, world! Good afternoon; good evening? Goodnight, world.");
  SendFinalResultAndWaitForEditableValue(
      "delete the previous sentence",
      "Hello, world! Good afternoon; good evening?");
  SendFinalResultAndWaitForEditableValue("delete the previous sentence",
                                         "Hello, world! Good afternoon;");
  SendFinalResultAndWaitForEditableValue("delete the previous sentence",
                                         "Hello, world!");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, DeletePrevSentTwoSentences) {
  SendFinalResultAndWaitForEditableValue("Hello, world. Goodnight, world.",
                                         "Hello, world. Goodnight, world.");
  SendFinalResultAndWaitForEditableValue("delete the previous sentence",
                                         "Hello, world.");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, DeletePrevSentMiddleOfSentence) {
  SendFinalResultAndWaitForEditableValue("Hello, world. Goodnight, world.",
                                         "Hello, world. Goodnight, world.");
  // Move the text caret into the middle of the second sentence.
  SendFinalResultAndWaitForCaretBoundsChanged("Move to the Previous character");
  SendFinalResultAndWaitForCaretBoundsChanged("Move to the Previous character");
  SendFinalResultAndWaitForEditableValue("delete the previous sentence",
                                         "Hello, world.d.");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, MoveByWord) {
  SendFinalResultAndWaitForEditableValue("This is a quiz", "This is a quiz");
  SendFinalResultAndWaitForCaretBoundsChanged("move to the previous word");
  SendFinalResultAndWaitForEditableValue("pop ", "This is a pop quiz");
  SendFinalResultAndWaitForCaretBoundsChanged("move to the next word");
  SendFinalResultAndWaitForEditableValue("folks!", "This is a pop quiz folks!");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, SmartDeletePhraseSimple) {
  SendFinalResultAndWaitForEditableValue("This is a difficult test",
                                         "This is a difficult test");
  SendFinalResultAndWaitForEditableValue("delete difficult", "This is a test");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest,
                       SmartDeletePhraseCaseInsensitive) {
  SendFinalResultAndWaitForEditableValue("This is a DIFFICULT test",
                                         "This is a DIFFICULT test");
  SendFinalResultAndWaitForEditableValue("delete difficult", "This is a test");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest,
                       SmartDeletePhraseDuplicateMatches) {
  SendFinalResultAndWaitForEditableValue("The cow jumped over the moon.",
                                         "The cow jumped over the moon.");
  // Deletes the right-most occurrence of "the".
  SendFinalResultAndWaitForEditableValue("delete the",
                                         "The cow jumped over moon.");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest,
                       SmartDeletePhraseDeletesLeftOfCaret) {
  SendFinalResultAndWaitForEditableValue("The cow jumped over the moon.",
                                         "The cow jumped over the moon.");
  SendFinalResultAndWaitForCaretBoundsChanged("move to the previous word");
  SendFinalResultAndWaitForCaretBoundsChanged("move to the previous word");
  SendFinalResultAndWaitForCaretBoundsChanged("move to the previous word");
  SendFinalResultAndWaitForEditableValue("delete the",
                                         "cow jumped over the moon.");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest,
                       SmartDeletePhraseDeletesAtWordBoundaries) {
  SendFinalResultAndWaitForEditableValue("A square is also a rectangle.",
                                         "A square is also a rectangle.");
  // Deletes the first word "a", not the first character "a".
  SendFinalResultAndWaitForEditableValue("delete a",
                                         "A square is also rectangle.");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, SmartReplacePhrase) {
  SendFinalResultAndWaitForEditableValue("This is a difficult test.",
                                         "This is a difficult test.");
  SendFinalResultAndWaitForEditableValue("replace difficult with simple",
                                         "This is a simple test.");
  SendFinalResultAndWaitForEditableValue("replace is with isn't",
                                         "This isn't a simple test.");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, SmartInsertBefore) {
  SendFinalResultAndWaitForEditableValue("This is a test.", "This is a test.");
  SendFinalResultAndWaitForEditableValue("insert simple before test",
                                         "This is a simple test.");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, SmartSelectBetween) {
  SendFinalResultAndWaitForEditableValue("This is a test.", "This is a test.");
  SendFinalResultAndWaitForSelectionChanged("select from this to test");
  SendFinalResultAndWaitForEditableValue("Hello world", "Hello world.");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, MoveBySentence) {
  SendFinalResultAndWaitForEditableValue("Hello world! Goodnight world?",
                                         "Hello world! Goodnight world?");
  SendFinalResultAndWaitForCaretBoundsChanged("move to the previous sentence");
  SendFinalResultAndWaitForEditableValue(
      "Good evening.", "Hello world! Good evening. Goodnight world?");
  SendFinalResultAndWaitForCaretBoundsChanged("move to the next sentence");
  SendFinalResultAndWaitForEditableValue(
      "Time for a midnight snack",
      "Hello world! Good evening. Goodnight world? Time for a midnight snack");
}

// CursorPosition... tests verify the new cursor position after a command is
// performed. The new cursor position is verified by inserting text after the
// command under test is performed.

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, CursorPositionDeleteSentence) {
  SendFinalResultAndWaitForEditableValue("First. Second.", "First. Second.");
  SendFinalResultAndWaitForEditableValue("delete the previous sentence",
                                         "First.");
  SendFinalResultAndWaitForEditableValue("Third.", "First. Third.");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, CursorPositionSmartDeletePhrase) {
  SendFinalResultAndWaitForEditableValue("This is a difficult test",
                                         "This is a difficult test");
  SendFinalResultAndWaitForCaretBoundsChanged("delete difficult");
  SendFinalResultAndWaitForEditableValue("simple", "This is a simple test");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest,
                       CursorPositionSmartReplacePhrase) {
  SendFinalResultAndWaitForEditableValue("This is a difficult test",
                                         "This is a difficult test");
  SendFinalResultAndWaitForCaretBoundsChanged("replace difficult with simple");
  SendFinalResultAndWaitForEditableValue("biology",
                                         "This is a simple biology test");
  SendFinalResultAndWaitForEditableValue(
      "and chemistry", "This is a simple biology and chemistry test");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, CursorPositionSmartInsertBefore) {
  SendFinalResultAndWaitForEditableValue("This is a test", "This is a test");
  SendFinalResultAndWaitForCaretBoundsChanged("insert simple before test");
  SendFinalResultAndWaitForEditableValue("biology",
                                         "This is a simple biology test");
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, SmartDeletePhraseLongContent) {
  if (!RunOnMultilineContent())
    return;

  std::string first_sentence_initial = R"(
    The dog (Canis familiaris or Canis lupus familiaris) is a domesticated
    descendant of the wolf.
  )";
  // The same as above, except the second instance of "familiaris" is removed.
  std::string first_sentence_final = R"(
    The dog (Canis familiaris or Canis lupus) is a domesticated
    descendant of the wolf.
  )";
  std::string remaining_text = R"(
    Also called the domestic dog, it is derived from an
    ancient, extinct wolf, and the modern wolf is the dog's nearest living
    relative. The dog was the first species to be domesticated, by
    hunter-gatherers over 15,000 years ago, before the development of
    agriculture. Due to their long association with humans, dogs have expanded
    to a large number of domestic individuals and gained the ability to thrive
    on a starch-rich diet that would be inadequate for other canids.

    The dog has been selectively bred over millennia for various behaviors,
    sensory capabilities, and physical attributes. Dog breeds vary widely in
    shape, size, and color. They perform many roles for humans, such as hunting,
    herding, pulling loads, protection, assisting police and the military,
    companionship, therapy, and aiding disabled people. Over the millennia, dogs
    became uniquely adapted to human behavior, and the human-canine bond has
    been a topic of frequent study. This influence on human society has given
    them the sobriquet of "man's best friend".
  )";

  std::string initial_value = first_sentence_initial + remaining_text;
  std::string final_value = first_sentence_final + remaining_text;
  SendFinalResultAndWaitForEditableValue(initial_value, initial_value);
  SendFinalResultAndWaitForEditableValue("delete familiaris", final_value);
}

IN_PROC_BROWSER_TEST_P(DictationCommandsTest, Metrics) {
  base::HistogramTester histogram_tester_;
  HistogramWaiter waiter(kPumpkinMetric);
  SendFinalResultAndWait("Undo");
  waiter.Wait();
  content::FetchHistogramsFromChildProcesses();
  metrics::SubprocessMetricsProvider::MergeHistogramDeltasForTesting();

  histogram_tester_.ExpectUniqueSample(/*name=*/kPumpkinMetric,
                                       /*sample=*/false,
                                       /*expected_bucket_count=*/1);
}

// Tests the behavior of the Dictation bubble UI.
class DictationUITest : public DictationTest {
 protected:
  DictationUITest() = default;
  ~DictationUITest() override = default;
  DictationUITest(const DictationUITest&) = delete;
  DictationUITest& operator=(const DictationUITest&) = delete;

  void SetUpOnMainThread() override {
    DictationTest::SetUpOnMainThread();
    dictation_bubble_test_helper_ =
        std::make_unique<DictationBubbleTestHelper>();
  }

  void WaitForProperties(
      bool visible,
      DictationBubbleIconType icon,
      const absl::optional<std::u16string>& text,
      const absl::optional<std::vector<std::u16string>>& hints) {
    WaitForVisibility(visible);
    WaitForVisibleIcon(icon);
    if (text.has_value())
      WaitForVisibleText(text.value());
    if (hints.has_value())
      WaitForVisibleHints(hints.value());
  }

 private:
  void WaitForVisibility(bool visible) {
    std::string error_message = "Still waiting for UI visibility: ";
    error_message += visible ? "true" : "false";
    SuccessWaiter(base::BindLambdaForTesting([&]() {
                    return dictation_bubble_test_helper_->IsVisible() ==
                           visible;
                  }),
                  error_message)
        .Wait();
  }

  void WaitForVisibleIcon(DictationBubbleIconType icon) {
    std::string error_message = "Still waiting for UI icon: " + ToString(icon);
    SuccessWaiter(base::BindLambdaForTesting([&]() {
                    return dictation_bubble_test_helper_->GetVisibleIcon() ==
                           icon;
                  }),
                  error_message)
        .Wait();
  }

  void WaitForVisibleText(const std::u16string& text) {
    std::string error_message =
        "Still waiting for UI text: " + base::UTF16ToUTF8(text);
    SuccessWaiter(base::BindLambdaForTesting([&]() {
                    return dictation_bubble_test_helper_->GetText() == text;
                  }),
                  error_message)
        .Wait();
  }

  void WaitForVisibleHints(const std::vector<std::u16string>& hints) {
    std::string error_message = base::UTF16ToUTF8(
        u"Still waiting for UI hints: " + base::JoinString(hints, u","));
    SuccessWaiter(
        base::BindLambdaForTesting([&]() {
          return dictation_bubble_test_helper_->HasVisibleHints(hints);
        }),
        error_message)
        .Wait();
  }

  std::unique_ptr<DictationBubbleTestHelper> dictation_bubble_test_helper_;
};

// Consistently failing on Linux ChromiumOS MSan (https://crbug.com/1302688).
#if defined(MEMORY_SANITIZER)
#define MAYBE_ShownWhenSpeechRecognitionStarts \
  DISABLED_ShownWhenSpeechRecognitionStarts
#define MAYBE_DisplaysInterimSpeechResults DISABLED_DisplaysInterimSpeechResults
#define MAYBE_DisplaysMacroSuccess DISABLED_DisplaysMacroSuccess
#define MAYBE_ResetsToStandbyModeAfterFinalSpeechResult \
  DISABLED_ResetsToStandbyModeAfterFinalSpeechResult
#define MAYBE_DisplaysMacroSuccess DISABLED_DisplaysMacroSuccess
#define MAYBE_HiddenWhenDictationDeactivates \
  DISABLED_HiddenWhenDictationDeactivates
#define MAYBE_StandbyHints DISABLED_StandbyHints
#define MAYBE_HintsShownWhenTextCommitted DISABLED_HintsShownWhenTextCommitted
#define MAYBE_HintsShownAfterTextSelected DISABLED_HintsShownAfterTextSelected
#define MAYBE_HintsShownAfterCommandExecuted \
  DISABLED_HintsShownAfterCommandExecuted
#else
#define MAYBE_ShownWhenSpeechRecognitionStarts ShownWhenSpeechRecognitionStarts
#define MAYBE_DisplaysInterimSpeechResults DisplaysInterimSpeechResults
#define MAYBE_DisplaysMacroSuccess DisplaysMacroSuccess
#define MAYBE_ResetsToStandbyModeAfterFinalSpeechResult \
  ResetsToStandbyModeAfterFinalSpeechResult
#define MAYBE_DisplaysMacroSuccess DisplaysMacroSuccess
#define MAYBE_HiddenWhenDictationDeactivates HiddenWhenDictationDeactivates
#define MAYBE_StandbyHints StandbyHints
#define MAYBE_HintsShownWhenTextCommitted HintsShownWhenTextCommitted
#define MAYBE_HintsShownAfterTextSelected HintsShownAfterTextSelected
#define MAYBE_HintsShownAfterCommandExecuted HintsShownAfterCommandExecuted
#endif

INSTANTIATE_TEST_SUITE_P(
    NetworkTextArea,
    DictationUITest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kNetwork,
                                 EditableType::kTextArea)));

INSTANTIATE_TEST_SUITE_P(
    OnDeviceTextArea,
    DictationUITest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kOnDevice,
                                 EditableType::kTextArea)));

IN_PROC_BROWSER_TEST_P(DictationUITest,
                       MAYBE_ShownWhenSpeechRecognitionStarts) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  WaitForProperties(/*visible=*/true,
                    /*icon=*/DictationBubbleIconType::kStandby,
                    /*text=*/absl::optional<std::u16string>(),
                    /*hints=*/absl::optional<std::vector<std::u16string>>());
}

IN_PROC_BROWSER_TEST_P(DictationUITest, MAYBE_DisplaysInterimSpeechResults) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  // Send an interim speech result.
  SendInterimResultAndWait("Testing");
  WaitForProperties(/*visible=*/true,
                    /*icon=*/DictationBubbleIconType::kHidden,
                    /*text=*/u"Testing",
                    /*hints=*/absl::optional<std::vector<std::u16string>>());
}

IN_PROC_BROWSER_TEST_P(DictationUITest, MAYBE_DisplaysMacroSuccess) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  // Perform a command.
  SendFinalResultAndWait("Select all");
  WaitForProperties(/*visible=*/true,
                    /*icon=*/DictationBubbleIconType::kMacroSuccess,
                    /*text=*/u"Select all",
                    /*hints=*/absl::optional<std::vector<std::u16string>>());
  // UI should return to standby mode after a timeout.
  WaitForProperties(/*visible=*/true,
                    /*icon=*/DictationBubbleIconType::kStandby,
                    /*text=*/std::u16string(),
                    /*hints=*/absl::optional<std::vector<std::u16string>>());
}

IN_PROC_BROWSER_TEST_P(DictationUITest,
                       MAYBE_ResetsToStandbyModeAfterFinalSpeechResult) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  WaitForProperties(/*visible=*/true,
                    /*icon=*/DictationBubbleIconType::kStandby,
                    /*text=*/absl::optional<std::u16string>(),
                    /*hints=*/absl::optional<std::vector<std::u16string>>());
  // Send an interim speech result.
  SendInterimResultAndWait("Testing");
  WaitForProperties(/*visible=*/true,
                    /*icon=*/DictationBubbleIconType::kHidden,
                    /*text=*/u"Testing",
                    /*hints=*/absl::optional<std::vector<std::u16string>>());
  // Send a final speech result. UI should return to standby mode.
  SendFinalResultAndWait("Testing 123");
  WaitForProperties(/*visible=*/true,
                    /*icon=*/DictationBubbleIconType::kStandby,
                    /*text=*/std::u16string(),
                    /*hints=*/absl::optional<std::vector<std::u16string>>());
}

IN_PROC_BROWSER_TEST_P(DictationUITest, MAYBE_HiddenWhenDictationDeactivates) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  WaitForProperties(/*visible=*/true,
                    /*icon=*/DictationBubbleIconType::kStandby,
                    /*text=*/absl::optional<std::u16string>(),
                    /*hints=*/absl::optional<std::vector<std::u16string>>());
  // The UI should be hidden when Dictation deactivates.
  ToggleDictationWithKeystroke();
  WaitForRecognitionStopped();
  WaitForProperties(/*visible=*/false,
                    /*icon=*/DictationBubbleIconType::kHidden,
                    /*text=*/std::u16string(),
                    /*hints=*/absl::optional<std::vector<std::u16string>>());
}

IN_PROC_BROWSER_TEST_P(DictationUITest, MAYBE_StandbyHints) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();
  WaitForProperties(/*visible=*/true,
                    /*icon=*/DictationBubbleIconType::kStandby,
                    /*text=*/absl::optional<std::u16string>(),
                    /*hints=*/absl::optional<std::vector<std::u16string>>());
  // Hints should show up after a few seconds without speech.
  WaitForProperties(
      /*visible=*/true,
      /*icon=*/DictationBubbleIconType::kStandby,
      /*text=*/absl::optional<std::u16string>(),
      /*hints=*/std::vector<std::u16string>{kTrySaying, kType, kHelp});
}

// Ensures that Search + D can be used to toggle Dictation when ChromeVox is
// active. Also verifies that ChromeVox announces hints when they are shown in
// the Dictation UI.
IN_PROC_BROWSER_TEST_P(DictationUITest, ChromeVoxAnnouncesHints) {
  // Setup ChromeVox first.
  test::SpeechMonitor sm;
  EXPECT_FALSE(GetManager()->IsSpokenFeedbackEnabled());
  extensions::ExtensionHostTestHelper host_helper(
      browser()->profile(), extension_misc::kChromeVoxExtensionId);
  EnableChromeVox();
  host_helper.WaitForHostCompletedFirstLoad();
  EXPECT_TRUE(GetManager()->IsSpokenFeedbackEnabled());

  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();

  // Hints should show up after a few seconds without speech.
  WaitForProperties(
      /*visible=*/true,
      /*icon=*/DictationBubbleIconType::kStandby,
      /*text=*/absl::optional<std::u16string>(),
      /*hints=*/std::vector<std::u16string>{kTrySaying, kType, kHelp});

  // Assert speech from ChromeVox.
  sm.ExpectSpeechPattern("Try saying*Type*Help*");
  sm.Replay();
}

IN_PROC_BROWSER_TEST_P(DictationUITest, MAYBE_HintsShownWhenTextCommitted) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();

  WaitForProperties(/*visible=*/true,
                    /*icon=*/DictationBubbleIconType::kStandby,
                    /*text=*/absl::optional<std::u16string>(),
                    /*hints=*/absl::optional<std::vector<std::u16string>>());

  // Send a final speech result. UI should return to standby mode.
  SendFinalResultAndWait("Testing");
  WaitForProperties(/*visible=*/true,
                    /*icon=*/DictationBubbleIconType::kStandby,
                    /*text=*/std::u16string(),
                    /*hints=*/absl::optional<std::vector<std::u16string>>());

  // Hints should show up after a few seconds without speech.
  WaitForProperties(
      /*visible=*/true,
      /*icon=*/DictationBubbleIconType::kStandby,
      /*text=*/absl::optional<std::u16string>(),
      /*hints=*/
      std::vector<std::u16string>{kTrySaying, kUndo, kDelete, kSelectAll,
                                  kHelp});
}

IN_PROC_BROWSER_TEST_P(DictationUITest, MAYBE_HintsShownAfterTextSelected) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();

  // Perform a select all command.
  SendFinalResultAndWaitForEditableValue("Vega is the brightest star in Lyra",
                                         "Vega is the brightest star in Lyra");
  SendFinalResultAndWaitForSelectionChanged("Select all");
  WaitForProperties(/*visible=*/true,
                    /*icon=*/DictationBubbleIconType::kMacroSuccess,
                    /*text=*/u"Select all",
                    /*hints=*/absl::optional<std::vector<std::u16string>>());

  // UI should return to standby mode with hints after a few seconds without
  // speech.
  WaitForProperties(
      /*visible=*/true,
      /*icon=*/DictationBubbleIconType::kStandby,
      /*text=*/absl::optional<std::u16string>(),
      /*hints=*/
      std::vector<std::u16string>{kTrySaying, kUnselect, kCopy, kDelete,
                                  kHelp});
}

IN_PROC_BROWSER_TEST_P(DictationUITest, MAYBE_HintsShownAfterCommandExecuted) {
  ToggleDictationWithKeystroke();
  WaitForRecognitionStarted();

  // Perform a command.
  SendFinalResultAndWait("Move to the previous character");
  WaitForProperties(/*visible=*/true,
                    /*icon=*/DictationBubbleIconType::kMacroSuccess,
                    /*text=*/u"Move to the previous character",
                    /*hints=*/absl::optional<std::vector<std::u16string>>());

  // UI should return to standby mode with hints after a few seconds without
  // speech.
  WaitForProperties(
      /*visible=*/true,
      /*icon=*/DictationBubbleIconType::kStandby,
      /*text=*/absl::optional<std::u16string>(),
      /*hints=*/std::vector<std::u16string>{kTrySaying, kUndo, kHelp});
}

// Tests behavior of Dictation and installation of Pumpkin.
class DictationPumpkinTest : public DictationTest {
 protected:
  DictationPumpkinTest() = default;
  ~DictationPumpkinTest() = default;
  DictationPumpkinTest(const DictationPumpkinTest&) = delete;
  DictationPumpkinTest& operator=(const DictationPumpkinTest&) = delete;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    DictationTest::SetUpCommandLine(command_line);
    scoped_feature_list_.InitAndEnableFeature(
        ::features::kExperimentalAccessibilityDictationWithPumpkin);
  }

  void SetUpOnMainThread() override {
    // Set the path to the Pumpkin test files. For more details, see the
    // `pumpkin_test_files` rule in the accessibility_common BUILD file.
    // Must be done before DictationTest::SetUpOnMainThread because the parent
    // class method will start up the extension and immediately request a
    // Pumpkin installation.
    base::ScopedAllowBlockingForTesting allow_blocking;
    base::FilePath gen_root_dir;
    ASSERT_TRUE(
        base::PathService::Get(base::DIR_GEN_TEST_DATA_ROOT, &gen_root_dir));
    base::FilePath pumpkin_test_file_path =
        gen_root_dir.AppendASCII(kPumpkinTestFilePath);
    ASSERT_TRUE(base::PathExists(pumpkin_test_file_path));
    AccessibilityManager::Get()->SetDlcPathForTest(pumpkin_test_file_path);

    DictationTest::SetUpOnMainThread();

    // Dictation will request a Pumpkin install when it starts up. Wait for
    // the install to succeed. Must be done after
    // DictationTest::SetUpOnMainThread because the steps here assume that the
    // extension is active.
    WaitForPumpkinTaggerReady();
    ToggleDictationWithKeystroke();
    WaitForRecognitionStarted();
  }

  void TearDownOnMainThread() override {
    ToggleDictationWithKeystroke();
    WaitForRecognitionStopped();
    DictationTest::TearDownOnMainThread();
  }

  void WaitForPumpkinTaggerReady() {
    std::string error_message = "Waiting for Pumpkin Tagger to initialize";
    SuccessWaiter(
        base::BindLambdaForTesting([&]() { return GetPumpkinTaggerReady(); }),
        error_message)
        .Wait();
  }

  bool GetPumpkinTaggerReady() {
    std::string script =
        "window.domAutomationController.send(String(accessibilityCommon."
        "dictation_.speechParser_.pumpkinParseStrategy_.pumpkinTaggerReady_));";
    return "true" == ExecuteAccessibilityCommonScript(script);
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
  base::ScopedTempDir pumpkin_root_dir_;
  std::vector<std::unique_ptr<base::ScopedTempDir>> sub_dirs_;
};

INSTANTIATE_TEST_SUITE_P(
    NetworkTextArea,
    DictationPumpkinTest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kNetwork,
                                 EditableType::kTextArea)));

INSTANTIATE_TEST_SUITE_P(
    NetworkInput,
    DictationPumpkinTest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kNetwork,
                                 EditableType::kInput)));

INSTANTIATE_TEST_SUITE_P(
    NetworkContentEditable,
    DictationPumpkinTest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kNetwork,
                                 EditableType::kContentEditable)));

INSTANTIATE_TEST_SUITE_P(
    OnDeviceTextArea,
    DictationPumpkinTest,
    ::testing::Values(TestConfig(speech::SpeechRecognitionType::kOnDevice,
                                 EditableType::kTextArea)));

// TODO(crbug.com/1368843): Test is flaky on MSAN builds.
#if defined(MEMORY_SANITIZER)
#define MAYBE_Input DISABLED_Input
#else
#define MAYBE_Input Input
#endif
IN_PROC_BROWSER_TEST_P(DictationPumpkinTest, MAYBE_Input) {
  SendFinalResultAndWaitForEditableValue("dictate hello", "Hello");
}

IN_PROC_BROWSER_TEST_P(DictationPumpkinTest, DeletePrevCharacter) {
  SendFinalResultAndWaitForEditableValue("Testing", "Testing");
  SendFinalResultAndWaitForEditableValue("Delete three characters", "Test");
  SendFinalResultAndWaitForEditableValue("backspace", "Tes");
}

IN_PROC_BROWSER_TEST_P(DictationPumpkinTest, NavByCharacter) {
  SendFinalResultAndWaitForEditableValue("Testing", "Testing");
  SendFinalResultAndWaitForCaretBoundsChanged("left three characters");
  SendFinalResultAndWaitForEditableValue("!", "Test!ing");
  SendFinalResultAndWaitForCaretBoundsChanged("right two characters");
  SendFinalResultAndWaitForEditableValue("@", "Test!in@g");
}

IN_PROC_BROWSER_TEST_P(DictationPumpkinTest, NavByLine) {
  if (!RunOnMultilineContent())
    return;

  std::string text = "Line1\nLine2\nLine3\nLine4";
  SendFinalResultAndWaitForEditableValue(text, text);
  SendFinalResultAndWaitForCaretBoundsChanged("Up two lines");
  std::string expected = "Line1\nLine2 insertion\nLine3\nLine4";
  SendFinalResultAndWaitForEditableValue("insertion", expected);
  SendFinalResultAndWaitForCaretBoundsChanged("down two lines");
  expected = "Line1\nLine2 insertion\nLine3\nLine4 second insertion";
  SendFinalResultAndWaitForEditableValue("second insertion", expected);
}

IN_PROC_BROWSER_TEST_P(DictationPumpkinTest, CutCopyPasteSelectAll) {
  SendFinalResultAndWaitForEditableValue("Star", "Star");
  SendFinalResultAndWaitForSelectionChanged("Select everything");
  SendFinalResultAndWaitForClipboardChanged("Copy selected text");
  EXPECT_EQ("Star", GetClipboardText());
  SendFinalResultAndWaitForSelectionChanged("unselect selection");
  SendFinalResultAndWaitForEditableValue("paste copied text", "StarStar");
  SendFinalResultAndWaitForSelectionChanged("highlight everything");
  SendFinalResultAndWaitForClipboardChanged("cut highlighted text");
  EXPECT_EQ("StarStar", GetClipboardText());
  WaitForEditableValue("");
  SendFinalResultAndWaitForEditableValue("paste the copied text", "StarStar");
}

IN_PROC_BROWSER_TEST_P(DictationPumpkinTest, UndoAndRedo) {
  SendFinalResultAndWaitForEditableValue("The constellation",
                                         "The constellation");
  SendFinalResultAndWaitForEditableValue("myra", "The constellation myra");
  SendFinalResultAndWaitForEditableValue("take that back", "The constellation");
  SendFinalResultAndWaitForEditableValue("Lyra", "The constellation lyra");
  SendFinalResultAndWaitForEditableValue("undo that", "The constellation");
  SendFinalResultAndWaitForEditableValue("redo that", "The constellation lyra");
}

IN_PROC_BROWSER_TEST_P(DictationPumpkinTest, Metrics) {
  base::HistogramTester histogram_tester_;
  HistogramWaiter waiter(kPumpkinMetric);
  SendFinalResultAndWaitForEditableValue("dictate hello", "Hello");
  waiter.Wait();
  content::FetchHistogramsFromChildProcesses();
  metrics::SubprocessMetricsProvider::MergeHistogramDeltasForTesting();

  histogram_tester_.ExpectUniqueSample(/*name=*/kPumpkinMetric,
                                       /*sample=*/true,
                                       /*expected_bucket_count=*/1);
}

}  // namespace ash
