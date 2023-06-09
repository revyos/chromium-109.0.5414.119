// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_IME_ASH_INPUT_METHOD_ASH_H_
#define UI_BASE_IME_ASH_INPUT_METHOD_ASH_H_

#include <stdint.h>

#include <memory>
#include <set>

#include "base/callback.h"
#include "base/callback_forward.h"
#include "base/component_export.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/ime/ash/text_input_method.h"
#include "ui/base/ime/ash/text_input_target.h"
#include "ui/base/ime/ash/typing_session_manager.h"
#include "ui/base/ime/character_composer.h"
#include "ui/base/ime/composition_text.h"
#include "ui/base/ime/input_method_base.h"
#include "ui/base/ime/text_input_client.h"

namespace ui {

namespace ime {
enum class KeyEventHandledState;
}

// A `ui::InputMethod` implementation for Ash.
class COMPONENT_EXPORT(UI_BASE_IME_ASH) InputMethodAsh
    : public InputMethodBase,
      public TextInputTarget {
 public:
  explicit InputMethodAsh(ImeKeyEventDispatcher* ime_key_event_dispatcher);

  InputMethodAsh(const InputMethodAsh&) = delete;
  InputMethodAsh& operator=(const InputMethodAsh&) = delete;

  ~InputMethodAsh() override;

  // Overridden from InputMethod:
  ui::EventDispatchDetails DispatchKeyEvent(ui::KeyEvent* event) override;
  void OnTextInputTypeChanged(TextInputClient* client) override;
  void OnCaretBoundsChanged(const TextInputClient* client) override;
  void CancelComposition(const TextInputClient* client) override;
  bool IsCandidatePopupOpen() const override;
  VirtualKeyboardController* GetVirtualKeyboardController() override;

  // Overridden from InputMethodBase:
  void OnFocus() override;
  void OnBlur() override;
  void OnTouch(ui::EventPointerType pointerType) override;
  void OnWillChangeFocusedClient(TextInputClient* focused_before,
                                 TextInputClient* focused) override;
  void OnDidChangeFocusedClient(TextInputClient* focused_before,
                                TextInputClient* focused) override;

  // ui::TextInputTarget overrides:
  void CommitText(
      const std::u16string& text,
      TextInputClient::InsertTextCursorBehavior cursor_behavior) override;
  bool SetCompositionRange(
      uint32_t before,
      uint32_t after,
      const std::vector<ui::ImeTextSpan>& text_spans) override;
  bool SetComposingRange(
      uint32_t start,
      uint32_t end,
      const std::vector<ui::ImeTextSpan>& text_spans) override;
  gfx::Range GetAutocorrectRange() override;
  gfx::Rect GetAutocorrectCharacterBounds() override;
  gfx::Rect GetTextFieldBounds() override;
  void SetAutocorrectRange(const gfx::Range& range,
                           SetAutocorrectRangeDoneCallback callback) override;
  absl::optional<GrammarFragment> GetGrammarFragmentAtCursor() override;
  bool ClearGrammarFragments(const gfx::Range& range) override;
  bool AddGrammarFragments(
      const std::vector<GrammarFragment>& fragments) override;
  bool SetSelectionRange(uint32_t start, uint32_t end) override;
  void UpdateCompositionText(const CompositionText& text,
                             uint32_t cursor_pos,
                             bool visible) override;
  void DeleteSurroundingText(uint32_t num_char16s_before_cursor,
                             uint32_t num_char16s_after_cursor) override;
  SurroundingTextInfo GetSurroundingTextInfo() override;
  void SendKeyEvent(KeyEvent* event) override;
  InputMethod* GetInputMethod() override;
  void ConfirmCompositionText(bool reset_engine, bool keep_selection) override;
  bool HasCompositionText() override;
  std::u16string GetCompositionText() override;
  ukm::SourceId GetClientSourceForMetrics() override;

 protected:
  // Converts |text| into CompositionText.
  CompositionText ExtractCompositionText(const CompositionText& text,
                                         uint32_t cursor_position) const;

  // Process a key returned from the input method.
  [[nodiscard]] virtual ui::EventDispatchDetails ProcessKeyEventPostIME(
      ui::KeyEvent* event,
      ui::ime::KeyEventHandledState handled_state,
      bool stopped_propagation);

  // Resets context and abandon all pending results and key events.
  // If |reset_engine| is true, a reset signal will be sent to the IME.
  void ResetContext(bool reset_engine = true);

 private:
  friend class TestableInputMethodAsh;

  // Representings a pending SetCompositionRange operation.
  struct PendingSetCompositionRange {
    PendingSetCompositionRange(const gfx::Range& range,
                               const std::vector<ui::ImeTextSpan>& text_spans);
    PendingSetCompositionRange(const PendingSetCompositionRange& other);
    ~PendingSetCompositionRange();

    gfx::Range range;
    std::vector<ui::ImeTextSpan> text_spans;
  };

  // Representings a pending CommitText operation.
  struct PendingCommit {
    std::u16string text;

    // Where the cursor should be placed in |text|.
    // 0 <= |cursor| <= |text.length()|.
    size_t cursor = 0;
  };

  struct PendingAutocorrectRange {
    PendingAutocorrectRange(const gfx::Range& range,
                            SetAutocorrectRangeDoneCallback callback);
    ~PendingAutocorrectRange();

    gfx::Range range;
    SetAutocorrectRangeDoneCallback callback;
  };

  // Checks the availability of focused text input client and update focus
  // state.
  void UpdateContextFocusState();

  // Processes a key event that was already filtered by the input method.
  // A VKEY_PROCESSKEY may be dispatched to the EventTargets.
  // It returns the result of whether the event has been stopped propagation
  // when dispatching post IME.
  [[nodiscard]] ui::EventDispatchDetails ProcessFilteredKeyPressEvent(
      ui::KeyEvent* event,
      bool only_dispatch_vkey_processkey);

  // Processes a key event that was not filtered by the input method.
  [[nodiscard]] ui::EventDispatchDetails ProcessUnfilteredKeyPressEvent(
      ui::KeyEvent* event);

  // Processes any pending input method operations that issued while handling
  // the key event. Does not do anything if there were no pending operations.
  void MaybeProcessPendingInputMethodResult(ui::KeyEvent* event, bool filtered);

  // Checks if the pending input method result needs inserting into the focused
  // text input client as a single character.
  bool NeedInsertChar() const;

  // Checks if there is pending input method result.
  bool HasInputMethodResult() const;

  // Passes keyevent and executes character composition if necessary. Returns
  // true if character composer comsumes key event.
  bool ExecuteCharacterComposer(const ui::KeyEvent& event);

  // Hides the composition text.
  void HidePreeditText();

  // Whether the focused text input client supports inline composition.
  bool CanComposeInline() const;

  TextInputMethod::InputContext GetInputContext() const;

  // Called from the engine when it completes processing.
  void ProcessKeyEventDone(ui::KeyEvent* event,
                           ui::ime::KeyEventHandledState handled_state);

  bool IsPasswordOrNoneInputFieldFocused();

  // Gets the bounds of the composition text or cursor in |client|.
  std::vector<gfx::Rect> GetCompositionBounds(const TextInputClient* client);

  // Sends a fake key event for IME composing without physical key events.
  // Returns true if the faked key event is stopped propagation.
  bool SendFakeProcessKeyEvent(bool pressed) const;

  // Pending composition text generated by the current pending key event.
  // It'll be sent to the focused text input client as soon as we receive the
  // processing result of the pending key event.
  absl::optional<CompositionText> pending_composition_;

  // Pending result text generated by the current pending key event.
  // It'll be sent to the focused text input client as soon as we receive the
  // processing result of the pending key event.
  absl::optional<PendingCommit> pending_commit_;

  std::u16string previous_surrounding_text_;
  gfx::Range previous_selection_range_;

  // Indicates if there is an ongoing composition text.
  bool composing_text_ = false;

  // Indicates if the composition text is changed or deleted.
  bool composition_changed_ = false;

  // Indicates whether there is a pending SetCompositionRange operation.
  absl::optional<PendingSetCompositionRange> pending_composition_range_;

  std::unique_ptr<PendingAutocorrectRange> pending_autocorrect_range_;

  // An object to compose a character from a sequence of key presses
  // including dead key etc.
  CharacterComposer character_composer_;

  // Indicates whether currently is handling a physical key event.
  // This is used in CommitText/UpdateCompositionText/etc.
  bool handling_key_event_ = false;

  TypingSessionManager typing_session_manager_;

  // Used for making callbacks.
  base::WeakPtrFactory<InputMethodAsh> weak_ptr_factory_{this};
};

}  // namespace ui

#endif  // UI_BASE_IME_ASH_INPUT_METHOD_ASH_H_
