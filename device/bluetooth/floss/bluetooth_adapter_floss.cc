// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/floss/bluetooth_adapter_floss.h"

#include "base/callback_helpers.h"
#include "base/containers/contains.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/notreached.h"
#include "base/observer_list.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/device_event_log/device_event_log.h"
#include "device/bluetooth/bluetooth_adapter.h"
#include "device/bluetooth/bluetooth_socket_thread.h"
#include "device/bluetooth/floss/bluetooth_advertisement_floss.h"
#include "device/bluetooth/floss/bluetooth_device_floss.h"
#include "device/bluetooth/floss/bluetooth_low_energy_scan_session_floss.h"
#include "device/bluetooth/floss/bluetooth_socket_floss.h"
#include "device/bluetooth/floss/floss_dbus_manager.h"
#include "device/bluetooth/floss/floss_lescan_client.h"
#include "device/bluetooth/floss/floss_socket_manager.h"
#include "device/bluetooth/public/cpp/bluetooth_address.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "device/bluetooth/chromeos/bluetooth_connection_logger.h"
#include "device/bluetooth/chromeos/bluetooth_utils.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/constants/devicetype.h"
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

namespace floss {

namespace {
using device::UMABluetoothDiscoverySessionOutcome;

UMABluetoothDiscoverySessionOutcome TranslateDiscoveryErrorToUMA(
    const Error& error) {
  // TODO(b/192289534) - Deal with UMA later
  return UMABluetoothDiscoverySessionOutcome::NOT_IMPLEMENTED;
}

// Helper function to gate init behind a check for Object Manager support.
void InitWhenObjectManagerKnown(base::OnceClosure callback) {
  FlossDBusManager::Get()->CallWhenObjectManagerSupportIsKnown(
      std::move(callback));
}

BluetoothDeviceFloss::ConnectErrorCode BtifStatusToConnectErrorCode(
    uint32_t status) {
  switch (static_cast<FlossAdapterClient::BtifStatus>(status)) {
    case FlossAdapterClient::BtifStatus::kFail:
      return BluetoothDeviceFloss::ConnectErrorCode::ERROR_FAILED;
    case FlossAdapterClient::BtifStatus::kAuthFailure:
      return BluetoothDeviceFloss::ConnectErrorCode::ERROR_AUTH_FAILED;
    case FlossAdapterClient::BtifStatus::kAuthRejected:
      return BluetoothDeviceFloss::ConnectErrorCode::ERROR_AUTH_REJECTED;
    case FlossAdapterClient::BtifStatus::kDone:
    case FlossAdapterClient::BtifStatus::kBusy:
      return BluetoothDeviceFloss::ConnectErrorCode::ERROR_INPROGRESS;
    case FlossAdapterClient::BtifStatus::kUnsupported:
      return BluetoothDeviceFloss::ConnectErrorCode::ERROR_UNSUPPORTED_DEVICE;
    default:
      return BluetoothDeviceFloss::ConnectErrorCode::ERROR_UNKNOWN;
  }
}

bool DeviceHasReadProperties(device::BluetoothDevice* device) {
  if (device) {
    return static_cast<BluetoothDeviceFloss*>(device)->HasReadProperties();
  }

  return false;
}

}  // namespace

// static
scoped_refptr<BluetoothAdapterFloss> BluetoothAdapterFloss::CreateAdapter() {
  return base::WrapRefCounted(new BluetoothAdapterFloss());
}

BluetoothAdapterFloss::BluetoothAdapterFloss() {
  ui_task_runner_ = base::ThreadTaskRunnerHandle::Get();
  socket_thread_ = device::BluetoothSocketThread::Get();
}

BluetoothAdapterFloss::~BluetoothAdapterFloss() {
  Shutdown();
}

void BluetoothAdapterFloss::Initialize(base::OnceClosure callback) {
  BLUETOOTH_LOG(EVENT) << "BluetoothAdapterFloss::Initialize";
  init_callback_ = std::move(callback);

  // Go ahead to Init() if object manager support is already known (e.g. when
  // using fake clients), otherwise find out object manager support first below.
  if (floss::FlossDBusManager::Get()->IsObjectManagerSupportKnown()) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(&BluetoothAdapterFloss::Init,
                                  weak_ptr_factory_.GetWeakPtr()));
    return;
  }

  // Queue a task to check for ObjectManager support and init once the support
  // is known.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&InitWhenObjectManagerKnown,
                     base::BindOnce(&BluetoothAdapterFloss::Init,
                                    weak_ptr_factory_.GetWeakPtr())));
}

void BluetoothAdapterFloss::Shutdown() {
  BLUETOOTH_LOG(EVENT) << "BluetoothAdapterFloss::Shutdown";

  if (dbus_is_shutdown_)
    return;

  if (!FlossDBusManager::Get()->IsObjectManagerSupported()) {
    dbus_is_shutdown_ = true;
    return;
  }

  if (IsPresent())
    RemoveAdapter();  // Cleans up devices and adapter observers.
  DCHECK(devices_.empty());

  // This may call unregister on advertisements that have already been
  // unregistered but that's fine. The advertisement object keeps a track of
  // the fact that it has been already unregistered and will call our empty
  // error callback with an "Already unregistered" error, which we'll ignore.
  for (const auto& adv : advertisements_) {
    adv->Stop(base::DoNothing(), base::DoNothing());
  }
  advertisements_.clear();

  FlossDBusManager::Get()->GetManagerClient()->RemoveObserver(this);
  dbus_is_shutdown_ = true;

  for (const auto& [key, scanner] : scanners_) {
    scanner->OnRelease();
  }
  scanners_.clear();
}

void BluetoothAdapterFloss::AddAdapterObservers() {
  DCHECK(FlossDBusManager::Get()->HasActiveAdapter());

  // Add any observers that depend on a specific observer.
  // FlossDBusManager::SwitchAdapter will control what the active adapter is
  // that we are controlling.
  FlossDBusManager::Get()->GetAdapterClient()->AddObserver(this);
  FlossDBusManager::Get()->GetLEScanClient()->AddObserver(this);
  FlossDBusManager::Get()->GetBatteryManagerClient()->AddObserver(this);
}

void BluetoothAdapterFloss::RemoveAdapter() {
  if (!FlossDBusManager::Get()->HasActiveAdapter())
    return;

  ClearAllDevices();

  // Clean up observers
  FlossDBusManager::Get()->GetAdapterClient()->RemoveObserver(this);
  FlossDBusManager::Get()->GetLEScanClient()->RemoveObserver(this);

  // Remove adapter by switching to an invalid adapter (cleans up DBus clients)
  // and then emitting |AdapterPresentChanged| to observers.
  FlossDBusManager::Get()->SwitchAdapter(FlossDBusManager::kInvalidAdapter);
  PresentChanged(false);
}

void BluetoothAdapterFloss::PopulateInitialDevices() {
  FlossDBusManager::Get()->GetAdapterClient()->GetBondedDevices();
  FlossDBusManager::Get()->GetAdapterClient()->GetConnectedDevices();
}

void BluetoothAdapterFloss::ClearAllDevices() {
  // Move all elements of the original devices list to a new list here,
  // leaving the original list empty so that when we send DeviceRemoved(),
  // GetDevices() returns no devices.
  DevicesMap devices_swapped;
  devices_swapped.swap(devices_);

  for (auto& iter : devices_swapped) {
    for (auto& observer : observers_)
      observer.DeviceRemoved(this, iter.second.get());
  }
}

void BluetoothAdapterFloss::Init() {
  // If dbus is shutdown or ObjectManager isn't supported, we just return
  // without initializing anything.
  if (dbus_is_shutdown_ ||
      !FlossDBusManager::Get()->IsObjectManagerSupported()) {
    BLUETOOTH_LOG(ERROR) << "Floss Adapter initialized without object manager";
    initialized_ = true;
    std::move(init_callback_).Run();
    return;
  }

  BLUETOOTH_LOG(EVENT) << "Floss Adapter Initialized";

  // Register for manager callbacks
  FlossDBusManager::Get()->GetManagerClient()->AddObserver(this);

  // Switch to adapter if the default adapter is present and enabled. If it is
  // not enabled, wait for upper layers to power it on.
  if (IsPresent()) {
    FlossManagerClient* manager = FlossDBusManager::Get()->GetManagerClient();
    int default_adapter = manager->GetDefaultAdapter();

    if (manager->GetAdapterEnabled(default_adapter)) {
      FlossDBusManager::Get()->SwitchAdapter(default_adapter);
      AddAdapterObservers();
    }
  }

  VLOG(1) << "BluetoothAdapterFloss::Init completed. Calling init callback.";
  initialized_ = true;
  std::move(init_callback_).Run();
}

BluetoothAdapterFloss::UUIDList BluetoothAdapterFloss::GetUUIDs() const {
  return {};
}

std::string BluetoothAdapterFloss::GetAddress() const {
  if (IsPowered()) {
    return FlossDBusManager::Get()->GetAdapterClient()->GetAddress();
  }

  return std::string();
}

std::string BluetoothAdapterFloss::GetName() const {
  if (!IsPresent())
    return std::string();

  return FlossDBusManager::Get()->GetAdapterClient()->GetName();
}

std::string BluetoothAdapterFloss::GetSystemName() const {
  // TODO(b/238230098): Floss should expose system information, i.e. stack name
  // and version.
  return "Floss";
}

void BluetoothAdapterFloss::SetName(const std::string& name,
                                    base::OnceClosure callback,
                                    ErrorCallback error_callback) {
  if (!IsPresent()) {
    BLUETOOTH_LOG(ERROR) << "SetName: " << name << ". Not Present!";
    std::move(error_callback).Run();
    return;
  }

  FlossDBusManager::Get()->GetAdapterClient()->SetName(
      base::BindOnce(&BluetoothAdapterFloss::OnMethodResponse,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     std::move(error_callback)),
      name);
}

bool BluetoothAdapterFloss::IsInitialized() const {
  return initialized_;
}

bool BluetoothAdapterFloss::IsPresent() const {
  // No clients will be working if object manager isn't supported or dbus is
  // shut down
  if (dbus_is_shutdown_ ||
      !FlossDBusManager::Get()->IsObjectManagerSupported()) {
    VLOG(1) << "BluetoothAdapterFloss::IsPresent = false (no object manager "
               "support or dbus is shut down)";
    return false;
  }

  FlossManagerClient* manager = FlossDBusManager::Get()->GetManagerClient();
  auto present = manager->GetAdapterPresent(manager->GetDefaultAdapter());

  return present;
}

bool BluetoothAdapterFloss::IsPowered() const {
  auto powered = FlossDBusManager::Get()->HasActiveAdapter();

  return powered;
}

void BluetoothAdapterFloss::SetPowered(bool powered,
                                       base::OnceClosure callback,
                                       ErrorCallback error_callback) {
  if (!IsPresent()) {
    BLUETOOTH_LOG(ERROR) << "SetPowered: " << powered << ". Not Present!";
    std::move(error_callback).Run();
    return;
  }

  BLUETOOTH_LOG(EVENT) << __func__ << ": " << powered;

  FlossDBusManager::Get()->GetManagerClient()->SetAdapterEnabled(
      FlossDBusManager::Get()->GetManagerClient()->GetDefaultAdapter(), powered,
      base::BindOnce(&BluetoothAdapterFloss::OnMethodResponse,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     std::move(error_callback)));
}

bool BluetoothAdapterFloss::IsDiscoverable() const {
  if (!IsPresent())
    return false;

  return FlossDBusManager::Get()->GetAdapterClient()->GetDiscoverable();
}

void BluetoothAdapterFloss::SetDiscoverable(bool discoverable,
                                            base::OnceClosure callback,
                                            ErrorCallback error_callback) {
  if (!IsPresent()) {
    BLUETOOTH_LOG(ERROR) << "SetDiscoverable: " << discoverable
                         << ". Not Present!";
    std::move(error_callback).Run();
    return;
  }

  FlossDBusManager::Get()->GetAdapterClient()->SetDiscoverable(
      base::BindOnce(&BluetoothAdapterFloss::OnMethodResponse,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     std::move(error_callback)),
      discoverable);
}

bool BluetoothAdapterFloss::IsDiscovering() const {
  if (!IsPresent())
    return false;

  return NumScanningDiscoverySessions() > 0;
}

std::unique_ptr<BluetoothDeviceFloss>
BluetoothAdapterFloss::CreateBluetoothDeviceFloss(FlossDeviceId device) {
  return std::make_unique<BluetoothDeviceFloss>(this, device, ui_task_runner_,
                                                socket_thread_);
}

void BluetoothAdapterFloss::OnMethodResponse(base::OnceClosure callback,
                                             ErrorCallback error_callback,
                                             DBusResult<Void> ret) {
  if (!ret.has_value()) {
    std::move(error_callback).Run();
    return;
  }

  std::move(callback).Run();
}

void BluetoothAdapterFloss::OnRepeatedDiscoverySessionResult(
    bool start_discovery,
    bool is_error,
    UMABluetoothDiscoverySessionOutcome outcome) {
  BLUETOOTH_LOG(DEBUG) << __func__ << ": Discovery result - is_error( "
                       << is_error
                       << "), outcome = " << static_cast<int>(outcome);

  // If starting discovery failed and we have active discovery sessions, mark
  // them as inactive.
  if (start_discovery && is_error && NumScanningDiscoverySessions() > 0) {
    BLUETOOTH_LOG(DEBUG) << "Marking sessions as inactive.";
    MarkDiscoverySessionsAsInactive();

    // If we failed to re-start a repeated discovery, that means the discovering
    // state is false and needs to be sent to observers (we won't receive
    // another discovering changed callback).
    for (auto& observer : observers_) {
      observer.AdapterDiscoveringChanged(this, /*discovering=*/false);
    }
  }
}

void BluetoothAdapterFloss::OnStartDiscovery(
    DiscoverySessionResultCallback callback,
    DBusResult<Void> ret) {
  if (!ret.has_value()) {
    // Adapter path only exists if active adapter hasn't disappeared
    auto adapter_path = FlossDBusManager::Get()->HasActiveAdapter()
                            ? FlossDBusManager::Get()
                                  ->GetAdapterClient()
                                  ->GetObjectPath()
                                  ->value()
                            : std::string();
    BLUETOOTH_LOG(ERROR) << adapter_path
                         << ": Failed to start discovery: " << ret.error();
    std::move(callback).Run(true, TranslateDiscoveryErrorToUMA(ret.error()));

    return;
  }

  BLUETOOTH_LOG(EVENT) << __func__;

  if (IsPresent()) {
    std::move(callback).Run(false,
                            UMABluetoothDiscoverySessionOutcome::SUCCESS);
  } else {
    std::move(callback).Run(
        true, UMABluetoothDiscoverySessionOutcome::ADAPTER_REMOVED);
  }
}

void BluetoothAdapterFloss::OnStopDiscovery(
    DiscoverySessionResultCallback callback,
    DBusResult<Void> ret) {
  if (!ret.has_value()) {
    // Adapter path only exists if active adapter hasn't disappeared
    auto adapter_path = FlossDBusManager::Get()->HasActiveAdapter()
                            ? FlossDBusManager::Get()
                                  ->GetAdapterClient()
                                  ->GetObjectPath()
                                  ->value()
                            : std::string();
    BLUETOOTH_LOG(ERROR) << adapter_path
                         << ": Failed to stop discovery: " << ret.error();
    std::move(callback).Run(true, TranslateDiscoveryErrorToUMA(ret.error()));

    return;
  }

  BLUETOOTH_LOG(EVENT) << __func__;

  DCHECK_GE(NumDiscoverySessions(), 0);
  std::move(callback).Run(false, UMABluetoothDiscoverySessionOutcome::SUCCESS);
}

void BluetoothAdapterFloss::OnInitializeDeviceProperties(
    BluetoothDeviceFloss* device_ptr) {
  for (auto& observer : observers_)
    observer.DeviceAdded(this, device_ptr);
}

void BluetoothAdapterFloss::OnGetConnectionState(const FlossDeviceId& device_id,
                                                 DBusResult<uint32_t> ret) {
  BluetoothDeviceFloss* device =
      static_cast<BluetoothDeviceFloss*>(GetDevice(device_id.address));

  if (!device) {
    LOG(WARNING) << "GetConnectionState returned for a non-existing device "
                 << device_id;
    return;
  }

  // Connected if connection state >= 1:
  // https://android.googlesource.com/platform/packages/modules/Bluetooth/+/84eff3217e552cbb3399e6deecdfce6748ae34ef/system/btif/src/btif_dm.cc#693
  device->SetConnectionState(*ret);

  // If the state is different than what is currently stored, update it.
  if ((*ret >= 1) != device->IsConnected()) {
    device->SetIsConnected(*ret >= 1);
    NotifyDeviceChanged(device);
    NotifyDeviceConnectedStateChanged(device, device->IsConnected());
  }
}

void BluetoothAdapterFloss::OnGetBondState(const FlossDeviceId& device_id,
                                           DBusResult<uint32_t> ret) {
  BluetoothDeviceFloss* device =
      static_cast<BluetoothDeviceFloss*>(GetDevice(device_id.address));

  if (!device) {
    LOG(WARNING) << "GetBondState returned for a non-existing device "
                 << device_id;
    return;
  }

  device->SetBondState(static_cast<FlossAdapterClient::BondState>(*ret));
  NotifyDevicePairedChanged(device, device->IsPaired());
}

// Announce to observers a change in the adapter state.
void BluetoothAdapterFloss::DiscoverableChanged(bool discoverable) {
  for (auto& observer : observers_) {
    observer.AdapterDiscoverableChanged(this, discoverable);
  }
}

void BluetoothAdapterFloss::DiscoveringChanged(bool discovering) {
  // If the adapter stopped discovery due to a reason other than a request by
  // us, reset the count to 0.
  BLUETOOTH_LOG(EVENT) << "Discovering changed: " << discovering;

  // While there are discovery sessions open, keep restarting discovery.
  if (!discovering && NumScanningDiscoverySessions() > 0) {
    FlossDBusManager::Get()->GetAdapterClient()->StartDiscovery(base::BindOnce(
        &BluetoothAdapterFloss::OnStartDiscovery,
        weak_ptr_factory_.GetWeakPtr(),
        base::BindOnce(&BluetoothAdapterFloss::OnRepeatedDiscoverySessionResult,
                       weak_ptr_factory_.GetWeakPtr(),
                       /*start_discovery=*/true)));

  } else {
    for (auto& observer : observers_) {
      observer.AdapterDiscoveringChanged(this, discovering);
    }
  }
}

void BluetoothAdapterFloss::PresentChanged(bool present) {
  for (auto& observer : observers_) {
    observer.AdapterPresentChanged(this, present);
  }
}

void BluetoothAdapterFloss::NotifyAdapterPoweredChanged(bool powered) {
  for (auto& observer : observers_) {
    observer.AdapterPoweredChanged(this, powered);
  }
}

void BluetoothAdapterFloss::NotifyDeviceConnectedStateChanged(
    BluetoothDeviceFloss* device,
    bool is_now_connected) {
  DCHECK_EQ(device->IsConnected(), is_now_connected);

#if BUILDFLAG(IS_CHROMEOS)
  if (is_now_connected) {
    device::BluetoothConnectionLogger::RecordDeviceConnected(
        device->GetIdentifier(), device->GetDeviceType());
  } else {
    device::RecordDeviceDisconnect(device->GetDeviceType());
  }

  // Also log the total number of connected devices. This uses a sampled
  // histogram rather than a enumeration.
  int count = 0;
  for (auto& [unused_address, current_device] : devices_) {
    if (current_device->IsPaired() && current_device->IsConnected()) {
      count++;
    }
  }

  UMA_HISTOGRAM_COUNTS_100("Bluetooth.ConnectedDeviceCount", count);
#endif

  BluetoothAdapter::NotifyDeviceConnectedStateChanged(device, is_now_connected);
}

// Observers

void BluetoothAdapterFloss::AdapterPresent(int adapter, bool present) {
  VLOG(1) << "BluetoothAdapterFloss: Adapter " << adapter
          << ", present: " << present;
  // TODO(b/191906229) - Support non-default adapters
  if (adapter !=
      FlossDBusManager::Get()->GetManagerClient()->GetDefaultAdapter()) {
    return;
  }

  // If default adapter isn't present, we need to clean up the dbus manager
  if (!present) {
    RemoveAdapter();
  } else {
    // Notify observers
    PresentChanged(present);
  }
}

void BluetoothAdapterFloss::AdapterEnabledChanged(int adapter, bool enabled) {
  VLOG(1) << "BluetoothAdapterFloss: Adapter " << adapter
          << ", enabled: " << enabled;

  // TODO(b/191906229) - Support non-default adapters
  if (adapter !=
      FlossDBusManager::Get()->GetManagerClient()->GetDefaultAdapter()) {
    VLOG(0) << __func__ << ": Adapter not default: "
            << FlossDBusManager::Get()->GetManagerClient()->GetDefaultAdapter();
    return;
  }

  if (enabled && !FlossDBusManager::Get()->HasActiveAdapter()) {
    FlossDBusManager::Get()->SwitchAdapter(adapter);
    AddAdapterObservers();
  } else if (!enabled && FlossDBusManager::Get()->HasActiveAdapter()) {
    FlossDBusManager::Get()->SwitchAdapter(FlossDBusManager::kInvalidAdapter);
  }

  if (enabled) {
    PopulateInitialDevices();
  } else {
    ClearAllDevices();
  }

  NotifyAdapterPoweredChanged(enabled);
}

void BluetoothAdapterFloss::AdapterDiscoveringChanged(bool state) {
  DCHECK(IsPresent());

  DiscoveringChanged(state);
}

void BluetoothAdapterFloss::AdapterFoundDevice(
    const FlossDeviceId& device_found) {
  DCHECK(FlossDBusManager::Get());
  DCHECK(IsPresent());

  BLUETOOTH_LOG(EVENT) << __func__ << device_found;

  auto device_floss = CreateBluetoothDeviceFloss(device_found);
  BluetoothDeviceFloss* new_device_ptr = nullptr;

  std::string canonical_address =
      device::CanonicalizeBluetoothAddress(device_floss->GetAddress());

  // Devices are newly found if they aren't in the devices_ map or they were
  // added via ScanResult (which doesn't trigger property reads).
  if (!base::Contains(devices_, canonical_address)) {
    new_device_ptr = device_floss.get();
    devices_.emplace(canonical_address, std::move(device_floss));
  } else if (!DeviceHasReadProperties(devices_[canonical_address].get())) {
    new_device_ptr =
        static_cast<BluetoothDeviceFloss*>(devices_[canonical_address].get());
  }

  // Trigger property reads for new devices.
  if (new_device_ptr) {
    new_device_ptr->InitializeDeviceProperties(
        base::BindOnce(&BluetoothAdapterFloss::OnInitializeDeviceProperties,
                       weak_ptr_factory_.GetWeakPtr(), new_device_ptr));

    // TODO(b/204708206): Convert "Paired" and "Connected" property into a
    // property framework.
    FlossDBusManager::Get()->GetAdapterClient()->GetBondState(
        base::BindOnce(&BluetoothAdapterFloss::OnGetBondState,
                       weak_ptr_factory_.GetWeakPtr(), device_found),
        device_found);
    FlossDBusManager::Get()->GetAdapterClient()->GetConnectionState(
        base::BindOnce(&BluetoothAdapterFloss::OnGetConnectionState,
                       weak_ptr_factory_.GetWeakPtr(), device_found),
        device_found);
    return;
  }

  BluetoothDeviceFloss* device =
      static_cast<BluetoothDeviceFloss*>(devices_[canonical_address].get());
  if (UpdateDevice(device, device_floss.get())) {
    for (auto& observer : observers_)
      observer.DeviceChanged(this, device);
  }
}

bool BluetoothAdapterFloss::UpdateDevice(BluetoothDeviceFloss* device,
                                         BluetoothDeviceFloss* new_device) {
  bool updated = false;

  if (new_device->GetName() && device->GetName() != new_device->GetName()) {
    device->SetName(new_device->GetName().value_or(""));
    updated = true;
  }

  return updated;
}

void BluetoothAdapterFloss::AdapterClearedDevice(
    const FlossDeviceId& device_cleared) {
  DCHECK(FlossDBusManager::Get());
  DCHECK(IsPresent());

  auto device_floss = CreateBluetoothDeviceFloss(device_cleared);
  std::string canonical_address =
      device::CanonicalizeBluetoothAddress(device_floss->GetAddress());
  if (base::Contains(devices_, canonical_address)) {
    BluetoothDeviceFloss* device_ptr = device_floss.get();
    BluetoothDeviceFloss* found_ptr = static_cast<BluetoothDeviceFloss*>(
        GetDevice(device_floss->GetAddress()));

    // Only remove devices from devices_ that are not paired or connected.
    if (!found_ptr || (!found_ptr->IsPaired() && !found_ptr->IsConnected())) {
      devices_.erase(canonical_address);
    }

    for (auto& observer : observers_)
      observer.DeviceRemoved(this, device_ptr);
  }

  BLUETOOTH_LOG(EVENT) << __func__ << device_cleared;
}

void BluetoothAdapterFloss::AdapterSspRequest(
    const FlossDeviceId& remote_device,
    uint32_t cod,
    FlossAdapterClient::BluetoothSspVariant variant,
    uint32_t passkey) {
  BluetoothDeviceFloss* device =
      static_cast<BluetoothDeviceFloss*>(GetDevice(remote_device.address));

  if (!device) {
    LOG(WARNING) << "SSP request for an unknown device";
    return;
  }

  BluetoothPairingFloss* pairing = device->pairing();

  if (!pairing) {
    LOG(WARNING) << "SSP request for an unknown pairing";
    return;
  }

  device::BluetoothDevice::PairingDelegate* pairing_delegate =
      pairing->pairing_delegate();

  if (!pairing_delegate) {
    LOG(WARNING) << "SSP request for an unknown delegate";
    return;
  }

  switch (variant) {
    case FlossAdapterClient::BluetoothSspVariant::kPasskeyConfirmation:
      pairing->SetPairingExpectation(
          BluetoothPairingFloss::PairingExpectation::kConfirmation);
      pairing_delegate->ConfirmPasskey(device, passkey);
      break;
    case FlossAdapterClient::BluetoothSspVariant::kPasskeyEntry:
      // TODO(b/202334519): Test with LEGO Mindstorms EV3.
      pairing->SetPairingExpectation(
          BluetoothPairingFloss::PairingExpectation::kPinCode);
      pairing_delegate->RequestPinCode(device);
      break;
    case FlossAdapterClient::BluetoothSspVariant::kConsent:
      // We don't need to ask pairing delegate for consent, because having a
      // pairing delegate means that a user is the initiator of this pairing.
      FlossDBusManager::Get()->GetAdapterClient()->SetPairingConfirmation(
          base::DoNothing(), remote_device, /*accept=*/true);
      device->ResetPairing();
      break;
    case FlossAdapterClient::BluetoothSspVariant::kPasskeyNotification:
      pairing_delegate->DisplayPasskey(device, passkey);
      break;
    default:
      LOG(ERROR) << "Unimplemented pairing method "
                 << static_cast<int>(variant);
  }
}

void BluetoothAdapterFloss::DeviceBondStateChanged(
    const FlossDeviceId& remote_device,
    uint32_t status,
    FlossAdapterClient::BondState bond_state) {
  std::string canonical_address =
      device::CanonicalizeBluetoothAddress(remote_device.address);

  if (!base::Contains(devices_, canonical_address)) {
    LOG(WARNING) << "Received BondStateChanged for a non-existent device";
    return;
  }

  BLUETOOTH_LOG(EVENT) << "BondStateChanged " << remote_device.address
                       << " state = " << static_cast<uint32_t>(bond_state)
                       << " status = " << status;

  BluetoothDeviceFloss* device =
      static_cast<BluetoothDeviceFloss*>(devices_[canonical_address].get());

  if (status != 0) {
    LOG(ERROR) << "Received BondStateChanged with error status = " << status;
    // TODO(b/192289534): Record status in UMA.
    device->TriggerConnectCallback(BtifStatusToConnectErrorCode(status));

    // Since we're no longer bonded, also remove this from found list.
    if (bond_state == FlossAdapterClient::BondState::kNotBonded) {
      AdapterClearedDevice(remote_device);
    }
    return;
  }

  if (device->GetBondState() == bond_state) {
    return;
  }

  device->SetBondState(bond_state);
  NotifyDeviceChanged(device);
  NotifyDevicePairedChanged(device, device->IsPaired());

  if (bond_state == FlossAdapterClient::BondState::kBonded) {
    device->ConnectAllEnabledProfiles();
  } else if (bond_state == FlossAdapterClient::BondState::kNotBonded) {
    // If we're no longer bonded (or paired/connected), we should clear the
    // device so it doesn't show up in found devices list.
    AdapterClearedDevice(remote_device);
  }
}

void BluetoothAdapterFloss::AdapterDeviceConnected(
    const FlossDeviceId& device_id) {
  DCHECK(FlossDBusManager::Get());
  DCHECK(IsPresent());

  BLUETOOTH_LOG(EVENT) << __func__ << ": " << device_id;

  BluetoothDeviceFloss* device =
      static_cast<BluetoothDeviceFloss*>(GetDevice(device_id.address));
  if (!device) {
    LOG(WARNING) << "Device connected for an unknown device "
                 << device_id.address;
    return;
  }

  // TODO(b/220387308): Querying connection state after connection can be racy
  // with pairing state. We may need a separate pairing callback from Floss.
  FlossDBusManager::Get()->GetAdapterClient()->GetConnectionState(
      base::BindOnce(&BluetoothAdapterFloss::OnGetConnectionState,
                     weak_ptr_factory_.GetWeakPtr(), device_id),
      device_id);

  device->SetIsConnected(true);
  NotifyDeviceChanged(device);
  NotifyDeviceConnectedStateChanged(device, true);
}

absl::optional<device::BluetoothDevice::BatteryType> variant_to_battery_type(
    const std::string& variant) {
  std::unordered_map<std::string, device::BluetoothDevice::BatteryType>
      battery_type_lookup = {
          {"", device::BluetoothDevice::BatteryType::kDefault},
          {"left", device::BluetoothDevice::BatteryType::kLeftBudTrueWireless},
          {"right",
           device::BluetoothDevice::BatteryType::kRightBudTrueWireless},
          {"case", device::BluetoothDevice::BatteryType::kCaseTrueWireless},
      };
  if (!base::Contains(battery_type_lookup, variant)) {
    return absl::nullopt;
  }
  return battery_type_lookup[variant];
}

void BluetoothAdapterFloss::BatteryInfoUpdated(std::string remote_address,
                                               BatterySet battery_set) {
  BluetoothDeviceFloss* device =
      static_cast<BluetoothDeviceFloss*>(GetDevice(remote_address));
  if (!device) {
    LOG(WARNING) << "BatterySet received for unknown device" << remote_address;
    return;
  }

  for (const auto& battery : battery_set.batteries) {
    absl::optional<device::BluetoothDevice::BatteryType> battery_type =
        variant_to_battery_type(battery.variant);
    if (!battery_type) {
      LOG(WARNING) << "Unable to convert to battery_type from "
                   << battery.variant;
      continue;
    }
    device->SetBatteryInfo(device::BluetoothDevice::BatteryInfo(
        battery_type.value(), battery.percentage));
  }
}

void BluetoothAdapterFloss::AdapterDeviceDisconnected(
    const FlossDeviceId& device_id) {
  DCHECK(FlossDBusManager::Get());
  DCHECK(IsPresent());

  BLUETOOTH_LOG(EVENT) << __func__ << ": " << device_id;

  BluetoothDeviceFloss* device =
      static_cast<BluetoothDeviceFloss*>(GetDevice(device_id.address));
  if (!device) {
    LOG(WARNING) << "Device disconnected for an unknown device "
                 << device_id.address;
    return;
  }

  device->SetIsConnected(false);
  NotifyDeviceChanged(device);
  NotifyDeviceConnectedStateChanged(device, false);
}

std::unordered_map<device::BluetoothDevice*, device::BluetoothDevice::UUIDSet>
BluetoothAdapterFloss::RetrieveGattConnectedDevicesWithDiscoveryFilter(
    const device::BluetoothDiscoveryFilter& discovery_filter) {
  NOTIMPLEMENTED();
  return {};
}

void BluetoothAdapterFloss::CreateRfcommService(
    const device::BluetoothUUID& uuid,
    const ServiceOptions& options,
    CreateServiceCallback callback,
    CreateServiceErrorCallback error_callback) {
  DCHECK(!dbus_is_shutdown_);
  BLUETOOTH_LOG(DEBUG) << "Creating RFCOMM service: " << uuid.canonical_value();
  scoped_refptr<BluetoothSocketFloss> socket =
      BluetoothSocketFloss::CreateBluetoothSocket(ui_task_runner_,
                                                  socket_thread_);

  socket->Listen(this, FlossSocketManager::SocketType::kRfcomm, uuid, options,
                 base::BindOnce(std::move(callback), socket),
                 std::move(error_callback));
}

void BluetoothAdapterFloss::CreateL2capService(
    const device::BluetoothUUID& uuid,
    const ServiceOptions& options,
    CreateServiceCallback callback,
    CreateServiceErrorCallback error_callback) {
  DCHECK(!dbus_is_shutdown_);
  BLUETOOTH_LOG(DEBUG) << "Creating L2CAP service: " << uuid.canonical_value();
  scoped_refptr<BluetoothSocketFloss> socket =
      BluetoothSocketFloss::CreateBluetoothSocket(ui_task_runner_,
                                                  socket_thread_);

  socket->Listen(this, FlossSocketManager::SocketType::kL2cap, uuid, options,
                 base::BindOnce(std::move(callback), socket),
                 std::move(error_callback));
}

void BluetoothAdapterFloss::RegisterAdvertisement(
    std::unique_ptr<device::BluetoothAdvertisement::Data> advertisement_data,
    CreateAdvertisementCallback callback,
    AdvertisementErrorCallback error_callback) {
  scoped_refptr<BluetoothAdvertisementFloss> advertisement(
      new BluetoothAdvertisementFloss(std::move(advertisement_data),
                                      interval_ms_, this));
  advertisement->Start(base::BindOnce(std::move(callback), advertisement),
                       std::move(error_callback));
  advertisements_.emplace_back(advertisement);
}

void BluetoothAdapterFloss::SetAdvertisingInterval(
    const base::TimeDelta& min,
    const base::TimeDelta& max,
    base::OnceClosure callback,
    AdvertisementErrorCallback r_callback) {
  interval_ms_ = static_cast<uint16_t>(
      std::min(static_cast<int64_t>(std::numeric_limits<uint16_t>::max()),
               min.InMilliseconds()));
  for (const auto& adv : advertisements_) {
    adv->SetAdvertisingInterval(interval_ms_, base::DoNothing(),
                                base::DoNothing());
  }
  std::move(callback).Run();
}

void BluetoothAdapterFloss::ResetAdvertising(
    base::OnceClosure callback,
    AdvertisementErrorCallback error_callback) {
  for (const auto& adv : advertisements_) {
    adv->Stop(base::DoNothing(), base::DoNothing());
  }
  std::move(callback).Run();
}

void BluetoothAdapterFloss::ConnectDevice(
    const std::string& address,
    const absl::optional<device::BluetoothDevice::AddressType>& address_type,
    ConnectDeviceCallback callback,
    ConnectDeviceErrorCallback error_callback) {
  NOTIMPLEMENTED();
}

device::BluetoothLocalGattService* BluetoothAdapterFloss::GetGattService(
    const std::string& identifier) const {
  return nullptr;
}

#if BUILDFLAG(IS_CHROMEOS)
void BluetoothAdapterFloss::SetServiceAllowList(const UUIDList& uuids,
                                                base::OnceClosure callback,
                                                ErrorCallback error_callback) {
  NOTIMPLEMENTED();
}

std::unique_ptr<device::BluetoothLowEnergyScanSession>
BluetoothAdapterFloss::StartLowEnergyScanSession(
    std::unique_ptr<device::BluetoothLowEnergyScanFilter> filter,
    base::WeakPtr<device::BluetoothLowEnergyScanSession::Delegate> delegate) {
  auto scan_session = std::make_unique<BluetoothLowEnergyScanSessionFloss>(
      delegate,
      base::BindOnce(&BluetoothAdapterFloss::OnLowEnergyScanSessionDestroyed,
                     weak_ptr_factory_.GetWeakPtr()));
  FlossDBusManager::Get()->GetLEScanClient()->RegisterScanner(base::BindOnce(
      &BluetoothAdapterFloss::OnRegisterScanner, weak_ptr_factory_.GetWeakPtr(),
      scan_session->GetWeakPtr()));
  return scan_session;
}

device::BluetoothAdapter::LowEnergyScanSessionHardwareOffloadingStatus
BluetoothAdapterFloss::GetLowEnergyScanSessionHardwareOffloadingStatus() {
  NOTIMPLEMENTED();
  return LowEnergyScanSessionHardwareOffloadingStatus::kNotSupported;
}
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_CHROMEOS_ASH)
void BluetoothAdapterFloss::SetStandardChromeOSAdapterName() {
  DCHECK(IsPresent());
  std::string alias = ash::GetDeviceBluetoothName(GetAddress());
  FlossDBusManager::Get()->GetAdapterClient()->SetName(base::DoNothing(),
                                                       alias);
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

void BluetoothAdapterFloss::ScannerRegistered(device::BluetoothUUID uuid,
                                              uint8_t scanner_id,
                                              GattStatus status) {
  BLUETOOTH_LOG(EVENT) << "Scanner registered with UUID = " << uuid
                       << ", scanner id = " << static_cast<int>(scanner_id)
                       << ", status = " << static_cast<int>(status);

  if (!base::Contains(scanners_, uuid)) {
    VLOG(1) << "ScannerRegistered but no longer exists " << uuid;
    return;
  }

  if (status != GattStatus::kSuccess) {
    BLUETOOTH_LOG(ERROR) << "Error registering scanner " << uuid
                         << ", status: " << static_cast<int>(status);
    scanners_[uuid]->OnActivate(scanner_id, /*success=*/false);
    return;
  }

  FlossDBusManager::Get()->GetLEScanClient()->StartScan(
      base::BindOnce(&BluetoothAdapterFloss::OnStartScan,
                     weak_ptr_factory_.GetWeakPtr(), uuid, scanner_id),
      scanner_id, ScanSettings{}, ScanFilter{});
}

void BluetoothAdapterFloss::ScanResultReceived(ScanResult scan_result) {
  BLUETOOTH_LOG(DEBUG) << __func__ << ": " << scan_result.address;

  BluetoothDeviceFloss* device_ptr;
  std::string canonical_address =
      device::CanonicalizeBluetoothAddress(scan_result.address);

  if (base::Contains(devices_, canonical_address)) {
    device_ptr =
        static_cast<BluetoothDeviceFloss*>(devices_[canonical_address].get());
    device_ptr->UpdateTimestamp();
  } else {
    auto device = CreateBluetoothDeviceFloss(FlossDeviceId(
        {.address = scan_result.address, .name = scan_result.name}));
    device_ptr = device.get();
    devices_.emplace(canonical_address, std::move(device));
  }

  device::BluetoothDevice::ServiceDataMap service_data_map;
  for (const auto& [uuid, bytes] : scan_result.service_data) {
    service_data_map[device::BluetoothUUID(uuid)] = bytes;
  }

  device_ptr->UpdateAdvertisementData(
      scan_result.rssi, scan_result.flags, scan_result.service_uuids,
      scan_result.tx_power, service_data_map,
      device::BluetoothDevice::ManufacturerDataMap(
          scan_result.manufacturer_data.begin(),
          scan_result.manufacturer_data.end()));

  for (auto& observer : observers_)
    observer.DeviceAdvertisementReceived(this, device_ptr, scan_result.rssi,
                                         scan_result.adv_data);

  // All scanners share scan results
  for (const auto& [key, scanner] : scanners_) {
    scanner->OnDeviceFound(device_ptr);
  }
}

void BluetoothAdapterFloss::ScanResultLost(ScanResult scan_result) {
  // TODO(b/217274013): This needs to be wired once filters are in place and
  // API has been defined on daemon
  BLUETOOTH_LOG(DEBUG) << __func__ << ": " << scan_result.address;

  auto device = CreateBluetoothDeviceFloss(FlossDeviceId(
      {.address = scan_result.address, .name = scan_result.name}));
  std::string canonical_address =
      device::CanonicalizeBluetoothAddress(device->GetAddress());
  if (!base::Contains(devices_, canonical_address)) {
    BLUETOOTH_LOG(EVENT) << __func__
                         << ": Device lost but never previously found: "
                         << scan_result.address;
    return;
  }

  BluetoothDeviceFloss* device_ptr = device.get();
  BluetoothDeviceFloss* found_ptr =
      static_cast<BluetoothDeviceFloss*>(GetDevice(device->GetAddress()));

  // Only remove devices from devices_ that are not paired or connected.
  if (!found_ptr || (!found_ptr->IsPaired() && !found_ptr->IsConnected())) {
    devices_.erase(canonical_address);
  }

  // All scanners share scan results
  for (const auto& [key, scanner] : scanners_) {
    scanner->OnDeviceLost(device_ptr);
  }
}

void BluetoothAdapterFloss::RemovePairingDelegateInternal(
    device::BluetoothDevice::PairingDelegate* pairing_delegate) {
  NOTIMPLEMENTED();
}

base::WeakPtr<device::BluetoothAdapter> BluetoothAdapterFloss::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

bool BluetoothAdapterFloss::SetPoweredImpl(bool powered) {
  NOTREACHED();
  return false;
}

void BluetoothAdapterFloss::StartScanWithFilter(
    std::unique_ptr<device::BluetoothDiscoveryFilter> discovery_filter,
    DiscoverySessionResultCallback callback) {
  // Also return ADAPTER_NOT_PRESENT if not powered.
  // TODO(b/193839304) - !IsPowered should return ADAPTER_NOT_POWERED
  if (!IsPresent() || !IsPowered()) {
    std::move(callback).Run(
        true, UMABluetoothDiscoverySessionOutcome::ADAPTER_NOT_PRESENT);
    return;
  }

  BLUETOOTH_LOG(EVENT) << __func__;

  // TODO(b/192251662) - Support scan filtering. For now, start scanning with no
  // filters in place.
  FlossDBusManager::Get()->GetAdapterClient()->StartDiscovery(
      base::BindOnce(&BluetoothAdapterFloss::OnStartDiscovery,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void BluetoothAdapterFloss::UpdateFilter(
    std::unique_ptr<device::BluetoothDiscoveryFilter> discovery_filter,
    DiscoverySessionResultCallback callback) {
  // Also return ADAPTER_NOT_PRESENT if not powered.
  // TODO(b/193839304) - !IsPowered should return ADAPTER_NOT_POWERED
  if (!IsPresent() || !IsPowered()) {
    std::move(callback).Run(
        true, UMABluetoothDiscoverySessionOutcome::ADAPTER_NOT_PRESENT);
    return;
  }

  BLUETOOTH_LOG(EVENT) << __func__;

  // TODO(b/192251662) - Support scan filtering. For now, always return success
  std::move(callback).Run(false, UMABluetoothDiscoverySessionOutcome::SUCCESS);
}

void BluetoothAdapterFloss::StopScan(DiscoverySessionResultCallback callback) {
  // Also return ADAPTER_NOT_PRESENT if not powered.
  // TODO(b/193839304) - !IsPowered should return ADAPTER_NOT_POWERED
  if (!IsPresent() || !IsPowered()) {
    std::move(callback).Run(
        /*is_error= */ false,
        UMABluetoothDiscoverySessionOutcome::ADAPTER_NOT_PRESENT);
    return;
  }

  BLUETOOTH_LOG(EVENT) << __func__;

  DCHECK_EQ(NumDiscoverySessions(), 0);
  FlossDBusManager::Get()->GetAdapterClient()->CancelDiscovery(
      base::BindOnce(&BluetoothAdapterFloss::OnStopDiscovery,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void BluetoothAdapterFloss::OnRegisterScanner(
    base::WeakPtr<BluetoothLowEnergyScanSessionFloss> scan_session,
    DBusResult<device::BluetoothUUID> ret) {
  if (!ret.has_value()) {
    BLUETOOTH_LOG(ERROR) << "Failed RegisterScanner: " << ret.error();
    return;
  }
  scan_session->OnRegistered(ret.value());
  scanners_[ret.value()] = scan_session;
  BLUETOOTH_LOG(EVENT) << "Registering scanner " << ret.value();
}

void BluetoothAdapterFloss::OnStartScan(
    device::BluetoothUUID uuid,
    uint8_t scanner_id,
    DBusResult<FlossDBusClient::BtifStatus> ret) {
  if (!base::Contains(scanners_, uuid)) {
    VLOG(1) << "Started scanning but scanner no longer exists " << uuid;
    return;
  }

  if (!ret.has_value() ||
      ret.value() != FlossDBusClient::BtifStatus::kSuccess) {
    BLUETOOTH_LOG(ERROR) << "Failed StartScan: " << ret.error()
                         << ", status: " << static_cast<uint32_t>(ret.value());
    scanners_[uuid]->OnActivate(scanner_id, /*success=*/false);
    return;
  }

  BLUETOOTH_LOG(EVENT) << "OnStartScan succeeded";
  scanners_[uuid]->OnActivate(scanner_id, /*success=*/true);
}

void BluetoothAdapterFloss::OnLowEnergyScanSessionDestroyed(
    const std::string& uuid_str) {
  BLUETOOTH_LOG(EVENT) << __func__ << ": UUID = " << uuid_str;

  device::BluetoothUUID uuid = device::BluetoothUUID(uuid_str);
  if (!base::Contains(scanners_, uuid)) {
    return;
  }

  uint8_t scanner_id = scanners_[uuid]->GetScannerId();
  scanners_.erase(uuid);

  FlossDBusManager::Get()->GetLEScanClient()->UnregisterScanner(
      base::BindOnce(&BluetoothAdapterFloss::OnUnregisterScanner,
                     weak_ptr_factory_.GetWeakPtr(), scanner_id),
      scanner_id);
}

void BluetoothAdapterFloss::OnUnregisterScanner(uint8_t scanner_id,
                                                DBusResult<bool> ret) {
  BLUETOOTH_LOG(EVENT) << __func__ << ": scanner_id = " << scanner_id;

  if (!ret.has_value()) {
    BLUETOOTH_LOG(ERROR) << "Failed UnregisterScanner: " << ret.error();
  }
}

}  // namespace floss
