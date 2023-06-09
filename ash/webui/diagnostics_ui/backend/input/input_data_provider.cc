// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/webui/diagnostics_ui/backend/input/input_data_provider.h"

#include <fcntl.h>
#include <linux/input.h>

#include <vector>

#include "ash/accelerators/accelerator_controller_impl.h"
#include "ash/constants/ash_features.h"
#include "ash/public/cpp/tablet_mode.h"
#include "ash/shell.h"
#include "ash/system/diagnostics/keyboard_input_log.h"
#include "ash/webui/diagnostics_ui/backend/input/event_watcher_factory.h"
#include "ash/webui/diagnostics_ui/backend/input/input_data_event_watcher.h"
#include "ash/webui/diagnostics_ui/backend/input/input_device_information.h"
#include "ash/webui/diagnostics_ui/backend/input/keyboard_input_data_event_watcher.h"
#include "ash/webui/diagnostics_ui/mojom/input_data_provider.mojom.h"
#include "ash/wm/window_util.h"
#include "base/logging.h"
#include "base/ranges/algorithm.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/display/screen.h"
#include "ui/events/devices/device_data_manager.h"
#include "ui/events/devices/input_device.h"

namespace ash {
namespace diagnostics {

namespace {

bool GetEventNodeId(base::FilePath path, int* id) {
  const std::string base_name_prefix = "event";

  std::string base_name = path.BaseName().value();
  if (!base::StartsWith(base_name, base_name_prefix))
    return false;
  base_name.erase(0, base_name_prefix.length());
  return base::StringToInt(base_name, id);
}

// Determine if this particular evdev provides touchpad or touchscreen input;
// we do not want stylus devices, which also claim to be touchscreens.
bool IsTouchInputDevice(InputDeviceInformation* device_info) {
  return (device_info->event_device_info.HasTouchpad() ||
          (device_info->event_device_info.HasTouchscreen() &&
           !device_info->event_device_info.HasStylus()));
}

}  // namespace

InputDataProvider::InputDataProvider(aura::Window* window,
                                     KeyboardInputLog* keyboard_input_log_ptr)
    : keyboard_input_log_ptr_(keyboard_input_log_ptr),
      device_manager_(ui::CreateDeviceManager()),
      watcher_factory_(std::make_unique<EventWatcherFactoryImpl>()) {
  Initialize(window);
}

InputDataProvider::InputDataProvider(
    aura::Window* window,
    std::unique_ptr<ui::DeviceManager> device_manager_for_test,
    std::unique_ptr<EventWatcherFactory> watcher_factory,
    KeyboardInputLog* keyboard_input_log_ptr)
    : keyboard_input_log_ptr_(keyboard_input_log_ptr),
      device_manager_(std::move(device_manager_for_test)),
      watcher_factory_(std::move(watcher_factory)) {
  Initialize(window);
}

InputDataProvider::~InputDataProvider() {
  BlockShortcuts(/*should_block=*/false);
  device_manager_->RemoveObserver(this);
  widget_->RemoveObserver(this);
  TabletMode::Get()->RemoveObserver(this);
  ash::Shell::Get()->display_configurator()->RemoveObserver(this);
}

// static
mojom::ConnectionType InputDataProvider::ConnectionTypeFromInputDeviceType(
    ui::InputDeviceType type) {
  switch (type) {
    case ui::InputDeviceType::INPUT_DEVICE_INTERNAL:
      return mojom::ConnectionType::kInternal;
    case ui::InputDeviceType::INPUT_DEVICE_USB:
      return mojom::ConnectionType::kUsb;
    case ui::InputDeviceType::INPUT_DEVICE_BLUETOOTH:
      return mojom::ConnectionType::kBluetooth;
    case ui::InputDeviceType::INPUT_DEVICE_UNKNOWN:
      return mojom::ConnectionType::kUnknown;
  }
}

void InputDataProvider::Initialize(aura::Window* window) {
  // Window and widget are needed for security enforcement.
  CHECK(window);
  widget_ = views::Widget::GetWidgetForNativeWindow(window);
  CHECK(widget_);
  device_manager_->AddObserver(this);
  device_manager_->ScanDevices(this);
  widget_->AddObserver(this);
  TabletMode::Get()->AddObserver(this);
  ash::Shell::Get()->display_configurator()->AddObserver(this);
  UpdateMaySendEvents();
}

void InputDataProvider::BindInterface(
    mojo::PendingReceiver<mojom::InputDataProvider> pending_receiver) {
  receiver_.reset();
  receiver_.Bind(std::move(pending_receiver));
}

void InputDataProvider::GetConnectedDevices(
    GetConnectedDevicesCallback callback) {
  std::vector<mojom::KeyboardInfoPtr> keyboard_vector;
  keyboard_vector.reserve(keyboards_.size());
  for (auto& keyboard_info : keyboards_) {
    keyboard_vector.push_back(keyboard_info.second.Clone());
  }

  std::vector<mojom::TouchDeviceInfoPtr> touch_device_vector;
  touch_device_vector.reserve(touch_devices_.size());
  for (auto& touch_device_info : touch_devices_) {
    touch_device_vector.push_back(touch_device_info.second.Clone());
  }

  base::ranges::sort(keyboard_vector, std::less<>(), &mojom::KeyboardInfo::id);
  base::ranges::sort(touch_device_vector, std::less<>(),
                     &mojom::TouchDeviceInfo::id);

  std::move(callback).Run(std::move(keyboard_vector),
                          std::move(touch_device_vector));
}

void InputDataProvider::ObserveConnectedDevices(
    mojo::PendingRemote<mojom::ConnectedDevicesObserver> observer) {
  connected_devices_observers_.Add(std::move(observer));
}

void InputDataProvider::OnWidgetVisibilityChanged(views::Widget* widget,
                                                  bool visible) {
  UpdateEventObservers();
}

void InputDataProvider::OnWidgetActivationChanged(views::Widget* widget,
                                                  bool active) {
  UpdateEventObservers();
}

void InputDataProvider::ObserveTabletMode(
    mojo::PendingRemote<mojom::TabletModeObserver> observer,
    ObserveTabletModeCallback callback) {
  tablet_mode_observer_ =
      mojo::Remote<mojom::TabletModeObserver>(std::move(observer));
  std::move(callback).Run(TabletMode::Get()->InTabletMode());
}

void InputDataProvider::OnTabletModeStarted() {
  if (tablet_mode_observer_.is_bound()) {
    tablet_mode_observer_->OnTabletModeChanged(/*is_tablet_mode=*/true);
  }
}

void InputDataProvider::OnTabletModeEnded() {
  if (tablet_mode_observer_.is_bound()) {
    tablet_mode_observer_->OnTabletModeChanged(/*is_tablet_mode=*/false);
  }
}

void InputDataProvider::ObserveInternalDisplayPowerState(
    mojo::PendingRemote<mojom::InternalDisplayPowerStateObserver> observer) {
  auto power_state =
      Shell::Get()->display_configurator()->current_power_state();
  is_internal_display_on_ =
      power_state != chromeos::DISPLAY_POWER_INTERNAL_OFF_EXTERNAL_ON;
  internal_display_power_state_observer_ =
      mojo::Remote<mojom::InternalDisplayPowerStateObserver>(
          std::move(observer));
}

void InputDataProvider::OnPowerStateChanged(
    chromeos::DisplayPowerState power_state) {
  if (internal_display_power_state_observer_.is_bound()) {
    // Only when the internal display is off and external is on, we grey out the
    // internal touchscreen test button.
    is_internal_display_on_ =
        power_state != chromeos::DISPLAY_POWER_INTERNAL_OFF_EXTERNAL_ON;
    internal_display_power_state_observer_->OnInternalDisplayPowerStateChanged(
        is_internal_display_on_);
  }
}

void InputDataProvider::MoveAppToTestingScreen(uint32_t evdev_id) {
  aura::Window* window = widget_->GetNativeWindow();
  const int64_t current_display_id =
      display::Screen::GetScreen()->GetDisplayNearestWindow(window).id();

  // Find the testing touchscreen device.
  auto it = touch_devices_.find((int)evdev_id);
  if (it == touch_devices_.end())
    return;

  // Use device name to find the targeting display id.
  // Since we use evdev_id as the device id in our implementation, which
  // does not match the device id from ui::DeviceDataManager. So we use
  // the name property to find the correct device. TODO(zhangwenyu): Double
  // check if each touchscreen device from DeviceDataManager has a unique
  // name.
  for (const ui::TouchscreenDevice& device :
       ui::DeviceDataManager::GetInstance()->GetTouchscreenDevices()) {
    if (device.name == it->second->name) {
      // Only move if the app is not already in the correct display.
      if (current_display_id != device.target_display_id &&
          device.target_display_id != display::kInvalidDisplayId &&
          window_util::MoveWindowToDisplay(window, device.target_display_id)) {
        // Only if window is successfully moved, we record the
        // `previous_display_id_` so that we can move the window back when the
        // tester is closed.
        previous_display_id_ = current_display_id;
      }
      // Early break the loop as we've found the matching device, no matter if
      // we have called the move function or not. e.g. if the device is already
      // in the correct display.
      break;
    }
  }
}

void InputDataProvider::MoveAppBackToPreviousScreen() {
  if (previous_display_id_ != display::kInvalidDisplayId) {
    window_util::MoveWindowToDisplay(widget_->GetNativeWindow(),
                                     previous_display_id_);
  }

  // Always reset previous_display_id_ after MoveAppBackToPreviousScreen is
  // called. So it won't affect next time the function is used.
  previous_display_id_ = display::kInvalidDisplayId;
}

void InputDataProvider::SetA11yTouchPassthrough(bool enabled) {
  widget_->GetNativeWindow()->SetProperty(
      aura::client::kAccessibilityTouchExplorationPassThrough, enabled);
}

void InputDataProvider::UpdateMaySendEvents() {
  const bool widget_open = !widget_->IsClosed();
  const bool widget_active = widget_->IsActive();
  const bool widget_visible = widget_->IsVisible();

  may_send_events_ = widget_open && widget_visible && widget_active;
}

void InputDataProvider::UpdateEventObservers() {
  const bool previous = may_send_events_;
  UpdateMaySendEvents();

  if (previous != may_send_events_) {
    // If there are no observers, then we never want to block shortcuts.
    // If there are observers, then we want to block when we are going to send
    // events.
    if (!keyboard_observers_.empty()) {
      BlockShortcuts(may_send_events_);
    }

    if (!may_send_events_)
      SendPauseEvents();
    else
      SendResumeEvents();
  }
}

void InputDataProvider::BlockShortcuts(bool should_block) {
  auto* accelerator_controller = Shell::Get()->accelerator_controller();
  DCHECK(accelerator_controller);
  accelerator_controller->SetPreventProcessingAccelerators(should_block);
}

void InputDataProvider::ForwardKeyboardInput(uint32_t id) {
  if (!keyboards_.contains(id)) {
    LOG(ERROR) << "Couldn't find keyboard with ID " << id
               << " when trying to forward input.";
    return;
  }

  // If we are going to send keyboard events, we need to block shortcuts
  BlockShortcuts(may_send_events_);
  keyboard_watchers_[id] = watcher_factory_->MakeKeyboardEventWatcher(
      id, weak_factory_.GetWeakPtr());
}

void InputDataProvider::UnforwardKeyboardInput(uint32_t id) {
  if (!keyboards_.contains(id)) {
    LOG(ERROR) << "Couldn't find keyboard with ID " << id
               << " when trying to unforward input.";
  }
  if (!keyboard_watchers_.erase(id)) {
    LOG(ERROR) << "Couldn't find keyboard watcher with ID " << id
               << " when trying to unforward input.";
  }

  if (IsLoggingEnabled()) {
    keyboard_input_log_ptr_->CreateLogAndRemoveKeyboard(id);
  }

  // If there are no more watchers, unblock shortcuts
  if (keyboard_watchers_.empty()) {
    BlockShortcuts(/*should_block=*/false);
  }
}

const std::string InputDataProvider::GetKeyboardName(uint32_t id) {
  auto iter = keyboards_.find(id);
  return iter == keyboards_.end() ? "" : iter->second->name;
}

bool InputDataProvider::IsLoggingEnabled() const {
  return keyboard_input_log_ptr_ != nullptr;
}

void InputDataProvider::OnObservedKeyboardInputDisconnect(
    uint32_t id,
    mojo::RemoteSetElementId) {
  if (!keyboard_observers_.contains(id)) {
    LOG(ERROR) << "received keyboard observer disconnect for ID " << id
               << " without observer.";
    return;
  }

  // When the last observer has been disconnected, stop forwarding events.
  if (keyboard_observers_[id]->empty()) {
    keyboard_observers_.erase(id);
    UnforwardKeyboardInput(id);

    // The observer RemoteSet remains empty at this point; if a new
    // observer comes in, we will Forward it again.
  }
}

void InputDataProvider::ObserveKeyEvents(
    uint32_t id,
    mojo::PendingRemote<mojom::KeyboardObserver> observer) {
  CHECK(widget_) << "Observing Key Events for input diagnostics not allowed "
                    "without widget to track focus.";

  if (!keyboards_.contains(id)) {
    LOG(ERROR) << "Couldn't find keyboard with ID " << id
               << " when trying to receive input.";
    return;
  }

  if (IsLoggingEnabled()) {
    keyboard_input_log_ptr_->AddKeyboard(id, GetKeyboardName(id));
  }

  // When keyboard observer remote set is constructed, establish the disconnect
  // handler.
  if (!keyboard_observers_.contains(id)) {
    keyboard_observers_[id] =
        std::make_unique<mojo::RemoteSet<mojom::KeyboardObserver>>();
    keyboard_observers_[id]->set_disconnect_handler(base::BindRepeating(
        &InputDataProvider::OnObservedKeyboardInputDisconnect,
        base::Unretained(this), id));
  }

  auto& observers = *keyboard_observers_[id];

  const auto observer_id = observers.Add(std::move(observer));

  // Ensure first callback is 'Paused' if we do not currently have focus
  if (!may_send_events_)
    observers.Get(observer_id)->OnKeyEventsPaused();

  // When we are adding the first observer, start forwarding events.
  if (observers.size() == 1)
    ForwardKeyboardInput(id);
}

void InputDataProvider::SendPauseEvents() {
  for (const auto& keyboard : keyboard_observers_) {
    for (const auto& observer : *keyboard.second) {
      observer->OnKeyEventsPaused();
    }
  }

  // Re-arm our log message for future events.
  logged_not_dispatching_key_events_ = false;
}

void InputDataProvider::SendResumeEvents() {
  for (const auto& keyboard : keyboard_observers_) {
    for (const auto& observer : *keyboard.second) {
      observer->OnKeyEventsResumed();
    }
  }
}

void InputDataProvider::OnDeviceEvent(const ui::DeviceEvent& event) {
  if (event.device_type() != ui::DeviceEvent::DeviceType::INPUT ||
      event.action_type() == ui::DeviceEvent::ActionType::CHANGE) {
    return;
  }

  int id = -1;
  if (!GetEventNodeId(event.path(), &id)) {
    LOG(ERROR) << "Ignoring DeviceEvent: invalid path " << event.path();
    return;
  }

  if (event.action_type() == ui::DeviceEvent::ActionType::ADD) {
    info_helper_.AsyncCall(&InputDeviceInfoHelper::GetDeviceInfo)
        .WithArgs(id, event.path())
        .Then(base::BindOnce(&InputDataProvider::ProcessDeviceInfo,
                             weak_factory_.GetWeakPtr()));

  } else {
    DCHECK(event.action_type() == ui::DeviceEvent::ActionType::REMOVE);
    if (keyboards_.contains(id)) {
      if (keyboard_observers_.erase(id)) {
        // Unref'ing the observers does not trigger their
        // OnObservedKeyboardInputDisconnect handlers (which would normally
        // clean up any watchers), so we must explicitly release the watchers
        // here.
        UnforwardKeyboardInput(id);
      }
      keyboards_.erase(id);
      keyboard_aux_data_.erase(id);
      for (const auto& observer : connected_devices_observers_) {
        observer->OnKeyboardDisconnected(id);
      }
    } else if (touch_devices_.contains(id)) {
      touch_devices_.erase(id);
      for (const auto& observer : connected_devices_observers_) {
        observer->OnTouchDeviceDisconnected(id);
      }
    }
  }
}

void InputDataProvider::ProcessDeviceInfo(
    std::unique_ptr<InputDeviceInformation> device_info) {
  if (device_info == nullptr) {
    return;
  }

  if (IsTouchInputDevice(device_info.get())) {
    AddTouchDevice(device_info.get());
  } else if (device_info->event_device_info.HasKeyboard()) {
    AddKeyboard(device_info.get());
  } else if (device_info->event_device_info.HasSwEvent(SW_TABLET_MODE)) {
    // Having a tablet mode switch indicates that this is a convertible, so the
    // top-right key of the keyboard is most likely to be lock.
    has_tablet_mode_switch_ = true;

    // Since this device might be processed after the internal keyboard, update
    // any internal keyboards that are already registered (except ones which we
    // know have Control Panel on the top-right key).
    for (const auto& keyboard_pair : keyboards_) {
      const mojom::KeyboardInfoPtr& keyboard = keyboard_pair.second;
      if (keyboard->connection_type == mojom::ConnectionType::kInternal &&
          keyboard->top_right_key != mojom::TopRightKey::kControlPanel) {
        keyboard->top_right_key = mojom::TopRightKey::kLock;
      }
    }
  }
}

void InputDataProvider::AddTouchDevice(
    const InputDeviceInformation* device_info) {
  touch_devices_[device_info->evdev_id] =
      touch_helper_.ConstructTouchDevice(device_info, is_internal_display_on_);

  for (const auto& observer : connected_devices_observers_) {
    observer->OnTouchDeviceConnected(
        touch_devices_[device_info->evdev_id]->Clone());
  }
}

void InputDataProvider::AddKeyboard(const InputDeviceInformation* device_info) {
  auto aux_data = std::make_unique<InputDataProviderKeyboard::AuxData>();

  mojom::KeyboardInfoPtr keyboard =
      keyboard_helper_.ConstructKeyboard(device_info, aux_data.get());
  if (!features::IsExternalKeyboardInDiagnosticsAppEnabled() &&
      keyboard->connection_type != mojom::ConnectionType::kInternal) {
    return;
  }
  keyboards_[device_info->evdev_id] = std::move(keyboard);
  if (device_info->connection_type == mojom::ConnectionType::kInternal &&
      keyboards_[device_info->evdev_id]->top_right_key ==
          mojom::TopRightKey::kUnknown) {
    // With some exceptions, convertibles (with tablet mode switches) tend to
    // have a lock key in the top-right of the keyboard, while clamshells tend
    // to have a power key.
    keyboards_[device_info->evdev_id]->top_right_key =
        has_tablet_mode_switch_ ? mojom::TopRightKey::kLock
                                : mojom::TopRightKey::kPower;
  }
  keyboard_aux_data_[device_info->evdev_id] = std::move(aux_data);

  for (const auto& observer : connected_devices_observers_) {
    observer->OnKeyboardConnected(keyboards_[device_info->evdev_id]->Clone());
  }
}

void InputDataProvider::SendInputKeyEvent(uint32_t id,
                                          uint32_t key_code,
                                          uint32_t scan_code,
                                          bool down) {
  CHECK(widget_) << "Sending Key Events for input diagnostics not allowed "
                    "without widget to track focus.";

  if (!keyboard_observers_.contains(id)) {
    LOG(ERROR) << "Couldn't find keyboard observer with ID " << id
               << " when trying to dispatch key.";
    return;
  }

  if (!may_send_events_) {
    if (!logged_not_dispatching_key_events_) {
      // Note: this will be common if the input diagnostics window is opened,
      // but not focused, so just log once.
      LOG(ERROR) << "Will not dispatch keys when diagnostics window does not "
                    "have focus.";
      logged_not_dispatching_key_events_ = true;
    }
    return;
  }

  mojom::KeyEventPtr event = keyboard_helper_.ConstructInputKeyEvent(
      keyboards_[id], keyboard_aux_data_[id].get(), key_code, scan_code, down);

  if (IsLoggingEnabled()) {
    keyboard_input_log_ptr_->RecordKeyPressForKeyboard(id, event.Clone());
  }

  const auto& observers = *keyboard_observers_[id];
  for (const auto& observer : observers) {
    observer->OnKeyEvent(event->Clone());
  }
}

}  // namespace diagnostics
}  // namespace ash
