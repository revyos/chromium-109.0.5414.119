// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_TELEMETRY_API_TELEMETRY_API_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_TELEMETRY_API_TELEMETRY_API_H_

#include <memory>

#include "build/chromeos_buildflags.h"
#include "chrome/browser/chromeos/extensions/telemetry/api/base_telemetry_extension_api_guard_function.h"
#include "chrome/browser/chromeos/extensions/telemetry/api/remote_probe_service_strategy.h"
#include "chromeos/crosapi/mojom/probe_service.mojom.h"
#include "extensions/browser/extension_function.h"
#include "extensions/browser/extension_function_histogram_value.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace chromeos {

class TelemetryApiFunctionBase : public BaseTelemetryExtensionApiGuardFunction {
 public:
  TelemetryApiFunctionBase();

  TelemetryApiFunctionBase(const TelemetryApiFunctionBase&) = delete;
  TelemetryApiFunctionBase& operator=(const TelemetryApiFunctionBase&) = delete;

 protected:
  ~TelemetryApiFunctionBase() override;

  mojo::Remote<crosapi::mojom::TelemetryProbeService>& GetRemoteService();

#if BUILDFLAG(IS_CHROMEOS_LACROS)
  bool IsCrosApiAvailable() override;
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)

 private:
  std::unique_ptr<RemoteProbeServiceStrategy> remote_probe_service_strategy_;
};

class OsTelemetryGetBatteryInfoFunction : public TelemetryApiFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.telemetry.getBatteryInfo",
                             OS_TELEMETRY_GETBATTERYINFO)

  OsTelemetryGetBatteryInfoFunction();
  OsTelemetryGetBatteryInfoFunction(const OsTelemetryGetBatteryInfoFunction&) =
      delete;
  OsTelemetryGetBatteryInfoFunction& operator=(
      const OsTelemetryGetBatteryInfoFunction&) = delete;

 private:
  ~OsTelemetryGetBatteryInfoFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;

  void OnResult(crosapi::mojom::ProbeTelemetryInfoPtr ptr);
};

class OsTelemetryGetNonRemovableBlockDevicesInfoFunction
    : public TelemetryApiFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.telemetry.getNonRemovableBlockDevicesInfo",
                             OS_TELEMETRY_GETNONREMOVABLEBLOCKDEVICESINFO)

  OsTelemetryGetNonRemovableBlockDevicesInfoFunction();
  OsTelemetryGetNonRemovableBlockDevicesInfoFunction(
      const OsTelemetryGetNonRemovableBlockDevicesInfoFunction&) = delete;
  OsTelemetryGetNonRemovableBlockDevicesInfoFunction& operator=(
      const OsTelemetryGetNonRemovableBlockDevicesInfoFunction&) = delete;

 private:
  ~OsTelemetryGetNonRemovableBlockDevicesInfoFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;

  void OnResult(crosapi::mojom::ProbeTelemetryInfoPtr ptr);
};

class OsTelemetryGetCpuInfoFunction : public TelemetryApiFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.telemetry.getCpuInfo", OS_TELEMETRY_GETCPUINFO)

  OsTelemetryGetCpuInfoFunction();
  OsTelemetryGetCpuInfoFunction(const OsTelemetryGetCpuInfoFunction&) = delete;
  OsTelemetryGetCpuInfoFunction& operator=(
      const OsTelemetryGetCpuInfoFunction&) = delete;

 private:
  ~OsTelemetryGetCpuInfoFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;

  void OnResult(crosapi::mojom::ProbeTelemetryInfoPtr ptr);
};

class OsTelemetryGetInternetConnectivityInfoFunction
    : public TelemetryApiFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.telemetry.getInternetConnectivityInfo",
                             OS_TELEMETRY_GETINTERNETCONNECTIVITYINFO)

  OsTelemetryGetInternetConnectivityInfoFunction();
  OsTelemetryGetInternetConnectivityInfoFunction(
      const OsTelemetryGetInternetConnectivityInfoFunction&) = delete;
  OsTelemetryGetInternetConnectivityInfoFunction& operator=(
      const OsTelemetryGetInternetConnectivityInfoFunction&) = delete;

 private:
  ~OsTelemetryGetInternetConnectivityInfoFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;

  void OnResult(crosapi::mojom::ProbeTelemetryInfoPtr ptr);
};

class OsTelemetryGetMemoryInfoFunction : public TelemetryApiFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.telemetry.getMemoryInfo",
                             OS_TELEMETRY_GETMEMORYINFO)

  OsTelemetryGetMemoryInfoFunction();
  OsTelemetryGetMemoryInfoFunction(const OsTelemetryGetMemoryInfoFunction&) =
      delete;
  OsTelemetryGetMemoryInfoFunction& operator=(
      const OsTelemetryGetMemoryInfoFunction&) = delete;

 private:
  ~OsTelemetryGetMemoryInfoFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;

  void OnResult(crosapi::mojom::ProbeTelemetryInfoPtr ptr);
};

class OsTelemetryGetOemDataFunction : public TelemetryApiFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.telemetry.getOemData", OS_TELEMETRY_GETOEMDATA)

  OsTelemetryGetOemDataFunction();
  OsTelemetryGetOemDataFunction(const OsTelemetryGetOemDataFunction&) = delete;
  OsTelemetryGetOemDataFunction& operator=(
      const OsTelemetryGetOemDataFunction&) = delete;

 private:
  ~OsTelemetryGetOemDataFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;

  void OnResult(crosapi::mojom::ProbeOemDataPtr ptr);
};

class OsTelemetryGetOsVersionInfoFunction : public TelemetryApiFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.telemetry.getOsVersionInfo",
                             OS_TELEMETRY_GETOSVERSIONINFO)

  OsTelemetryGetOsVersionInfoFunction();
  OsTelemetryGetOsVersionInfoFunction(
      const OsTelemetryGetOsVersionInfoFunction&) = delete;
  OsTelemetryGetOsVersionInfoFunction& operator=(
      const OsTelemetryGetOsVersionInfoFunction&) = delete;

 private:
  ~OsTelemetryGetOsVersionInfoFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;

  void OnResult(crosapi::mojom::ProbeTelemetryInfoPtr ptr);
};

class OsTelemetryGetVpdInfoFunction : public TelemetryApiFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.telemetry.getVpdInfo", OS_TELEMETRY_GETVPDINFO)

  OsTelemetryGetVpdInfoFunction();
  OsTelemetryGetVpdInfoFunction(const OsTelemetryGetVpdInfoFunction&) = delete;
  OsTelemetryGetVpdInfoFunction& operator=(
      const OsTelemetryGetVpdInfoFunction&) = delete;

 private:
  ~OsTelemetryGetVpdInfoFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;

  void OnResult(crosapi::mojom::ProbeTelemetryInfoPtr ptr);
};

class OsTelemetryGetStatefulPartitionInfoFunction
    : public TelemetryApiFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.telemetry.getStatefulPartitionInfo",
                             OS_TELEMETRY_GETSTATEFULPARTITIONINFO)

  OsTelemetryGetStatefulPartitionInfoFunction();
  OsTelemetryGetStatefulPartitionInfoFunction(
      const OsTelemetryGetStatefulPartitionInfoFunction&) = delete;
  OsTelemetryGetStatefulPartitionInfoFunction& operator=(
      const OsTelemetryGetStatefulPartitionInfoFunction&) = delete;

 private:
  ~OsTelemetryGetStatefulPartitionInfoFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;

  void OnResult(crosapi::mojom::ProbeTelemetryInfoPtr ptr);
};

class OsTelemetryGetTpmInfoFunction : public TelemetryApiFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.telemetry.getTpmInfo", OS_TELEMETRY_GETTPMINFO)

  OsTelemetryGetTpmInfoFunction();
  OsTelemetryGetTpmInfoFunction(const OsTelemetryGetTpmInfoFunction&) = delete;
  OsTelemetryGetTpmInfoFunction& operator=(
      const OsTelemetryGetTpmInfoFunction&) = delete;

 private:
  ~OsTelemetryGetTpmInfoFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;

  void OnResult(crosapi::mojom::ProbeTelemetryInfoPtr ptr);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_TELEMETRY_API_TELEMETRY_API_H_
