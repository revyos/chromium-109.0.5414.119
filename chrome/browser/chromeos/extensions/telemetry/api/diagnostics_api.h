// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_TELEMETRY_API_DIAGNOSTICS_API_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_TELEMETRY_API_DIAGNOSTICS_API_H_

#include <memory>

#include "build/chromeos_buildflags.h"
#include "chrome/browser/chromeos/extensions/telemetry/api/base_telemetry_extension_api_guard_function.h"
#include "chrome/browser/chromeos/extensions/telemetry/api/remote_diagnostics_service_strategy.h"
#include "chromeos/crosapi/mojom/diagnostics_service.mojom.h"
#include "extensions/browser/extension_function.h"
#include "extensions/browser/extension_function_histogram_value.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace chromeos {

class DiagnosticsApiFunctionBase
    : public BaseTelemetryExtensionApiGuardFunction {
 public:
  DiagnosticsApiFunctionBase();

  DiagnosticsApiFunctionBase(const DiagnosticsApiFunctionBase&) = delete;
  DiagnosticsApiFunctionBase& operator=(const DiagnosticsApiFunctionBase&) =
      delete;

 protected:
  ~DiagnosticsApiFunctionBase() override;

  mojo::Remote<crosapi::mojom::DiagnosticsService>& GetRemoteService();

#if BUILDFLAG(IS_CHROMEOS_LACROS)
  bool IsCrosApiAvailable() override;
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)

 private:
  std::unique_ptr<RemoteDiagnosticsServiceStrategy>
      remote_diagnostics_service_strategy_;
};

class OsDiagnosticsGetAvailableRoutinesFunction
    : public DiagnosticsApiFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.getAvailableRoutines",
                             OS_DIAGNOSTICS_GETAVAILABLEROUTINES)

  OsDiagnosticsGetAvailableRoutinesFunction();
  OsDiagnosticsGetAvailableRoutinesFunction(
      const OsDiagnosticsGetAvailableRoutinesFunction&) = delete;
  OsDiagnosticsGetAvailableRoutinesFunction& operator=(
      const OsDiagnosticsGetAvailableRoutinesFunction&) = delete;

 private:
  ~OsDiagnosticsGetAvailableRoutinesFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;

  void OnResult(
      const std::vector<crosapi::mojom::DiagnosticsRoutineEnum>& routines);
};

class OsDiagnosticsGetRoutineUpdateFunction
    : public DiagnosticsApiFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.getRoutineUpdate",
                             OS_DIAGNOSTICS_GETROUTINEUPDATE)

  OsDiagnosticsGetRoutineUpdateFunction();
  OsDiagnosticsGetRoutineUpdateFunction(
      const OsDiagnosticsGetRoutineUpdateFunction&) = delete;
  OsDiagnosticsGetRoutineUpdateFunction& operator=(
      const OsDiagnosticsGetRoutineUpdateFunction&) = delete;

 private:
  ~OsDiagnosticsGetRoutineUpdateFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;

  void OnResult(crosapi::mojom::DiagnosticsRoutineUpdatePtr ptr);
};

class DiagnosticsApiRunRoutineFunctionBase : public DiagnosticsApiFunctionBase {
 public:
  DiagnosticsApiRunRoutineFunctionBase();

  DiagnosticsApiRunRoutineFunctionBase(
      const DiagnosticsApiRunRoutineFunctionBase&) = delete;
  DiagnosticsApiRunRoutineFunctionBase& operator=(
      const DiagnosticsApiRunRoutineFunctionBase&) = delete;

  void OnResult(crosapi::mojom::DiagnosticsRunRoutineResponsePtr ptr);

 protected:
  ~DiagnosticsApiRunRoutineFunctionBase() override;
};

class OsDiagnosticsRunAcPowerRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runAcPowerRoutine",
                             OS_DIAGNOSTICS_RUNACPOWERROUTINE)

  OsDiagnosticsRunAcPowerRoutineFunction();
  OsDiagnosticsRunAcPowerRoutineFunction(
      const OsDiagnosticsRunAcPowerRoutineFunction&) = delete;
  OsDiagnosticsRunAcPowerRoutineFunction& operator=(
      const OsDiagnosticsRunAcPowerRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunAcPowerRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunBatteryCapacityRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runBatteryCapacityRoutine",
                             OS_DIAGNOSTICS_RUNBATTERYCAPACITYROUTINE)

  OsDiagnosticsRunBatteryCapacityRoutineFunction();
  OsDiagnosticsRunBatteryCapacityRoutineFunction(
      const OsDiagnosticsRunBatteryCapacityRoutineFunction&) = delete;
  OsDiagnosticsRunBatteryCapacityRoutineFunction& operator=(
      const OsDiagnosticsRunBatteryCapacityRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunBatteryCapacityRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunBatteryChargeRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runBatteryChargeRoutine",
                             OS_DIAGNOSTICS_RUNBATTERYCHARGEROUTINE)

  OsDiagnosticsRunBatteryChargeRoutineFunction();
  OsDiagnosticsRunBatteryChargeRoutineFunction(
      const OsDiagnosticsRunBatteryChargeRoutineFunction&) = delete;
  OsDiagnosticsRunBatteryChargeRoutineFunction& operator=(
      const OsDiagnosticsRunBatteryChargeRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunBatteryChargeRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunBatteryDischargeRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runBatteryDischargeRoutine",
                             OS_DIAGNOSTICS_RUNBATTERYDISCHARGEROUTINE)

  OsDiagnosticsRunBatteryDischargeRoutineFunction();
  OsDiagnosticsRunBatteryDischargeRoutineFunction(
      const OsDiagnosticsRunBatteryDischargeRoutineFunction&) = delete;
  OsDiagnosticsRunBatteryDischargeRoutineFunction& operator=(
      const OsDiagnosticsRunBatteryDischargeRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunBatteryDischargeRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunBatteryHealthRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runBatteryHealthRoutine",
                             OS_DIAGNOSTICS_RUNBATTERYHEALTHROUTINE)

  OsDiagnosticsRunBatteryHealthRoutineFunction();
  OsDiagnosticsRunBatteryHealthRoutineFunction(
      const OsDiagnosticsRunBatteryHealthRoutineFunction&) = delete;
  OsDiagnosticsRunBatteryHealthRoutineFunction& operator=(
      const OsDiagnosticsRunBatteryHealthRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunBatteryHealthRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunCpuCacheRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runCpuCacheRoutine",
                             OS_DIAGNOSTICS_RUNCPUCACHEROUTINE)

  OsDiagnosticsRunCpuCacheRoutineFunction();
  OsDiagnosticsRunCpuCacheRoutineFunction(
      const OsDiagnosticsRunCpuCacheRoutineFunction&) = delete;
  OsDiagnosticsRunCpuCacheRoutineFunction& operator=(
      const OsDiagnosticsRunCpuCacheRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunCpuCacheRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunCpuFloatingPointAccuracyRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION(
      "os.diagnostics.runCpuFloatingPointAccuracyRoutine",
      OS_DIAGNOSTICS_RUNCPUFLOATINGPOINTACCURACYROUTINE)

  OsDiagnosticsRunCpuFloatingPointAccuracyRoutineFunction();
  OsDiagnosticsRunCpuFloatingPointAccuracyRoutineFunction(
      const OsDiagnosticsRunCpuFloatingPointAccuracyRoutineFunction&) = delete;
  OsDiagnosticsRunCpuFloatingPointAccuracyRoutineFunction& operator=(
      const OsDiagnosticsRunCpuFloatingPointAccuracyRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunCpuFloatingPointAccuracyRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunCpuPrimeSearchRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runCpuPrimeSearchRoutine",
                             OS_DIAGNOSTICS_RUNCPUPRIMESEARCHROUTINE)

  OsDiagnosticsRunCpuPrimeSearchRoutineFunction();
  OsDiagnosticsRunCpuPrimeSearchRoutineFunction(
      const OsDiagnosticsRunCpuPrimeSearchRoutineFunction&) = delete;
  OsDiagnosticsRunCpuPrimeSearchRoutineFunction& operator=(
      const OsDiagnosticsRunCpuPrimeSearchRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunCpuPrimeSearchRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunCpuStressRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runCpuStressRoutine",
                             OS_DIAGNOSTICS_RUNCPUSTRESSROUTINE)

  OsDiagnosticsRunCpuStressRoutineFunction();
  OsDiagnosticsRunCpuStressRoutineFunction(
      const OsDiagnosticsRunCpuStressRoutineFunction&) = delete;
  OsDiagnosticsRunCpuStressRoutineFunction& operator=(
      const OsDiagnosticsRunCpuStressRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunCpuStressRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunDiskReadRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runDiskReadRoutine",
                             OS_DIAGNOSTICS_RUNDISKREADROUTINE)

  OsDiagnosticsRunDiskReadRoutineFunction();
  OsDiagnosticsRunDiskReadRoutineFunction(
      const OsDiagnosticsRunDiskReadRoutineFunction&) = delete;
  OsDiagnosticsRunDiskReadRoutineFunction& operator=(
      const OsDiagnosticsRunDiskReadRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunDiskReadRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunDnsResolutionRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runDnsResolutionRoutine",
                             OS_DIAGNOSTICS_RUNDNSRESOLUTIONROUTINE)

  OsDiagnosticsRunDnsResolutionRoutineFunction();
  OsDiagnosticsRunDnsResolutionRoutineFunction(
      const OsDiagnosticsRunDnsResolutionRoutineFunction&) = delete;
  OsDiagnosticsRunDnsResolutionRoutineFunction& operator=(
      const OsDiagnosticsRunDnsResolutionRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunDnsResolutionRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunDnsResolverPresentRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runDnsResolverPresentRoutine",
                             OS_DIAGNOSTICS_RUNDNSRESOLVERPRESENTROUTINE)

  OsDiagnosticsRunDnsResolverPresentRoutineFunction();
  OsDiagnosticsRunDnsResolverPresentRoutineFunction(
      const OsDiagnosticsRunDnsResolverPresentRoutineFunction&) = delete;
  OsDiagnosticsRunDnsResolverPresentRoutineFunction& operator=(
      const OsDiagnosticsRunDnsResolverPresentRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunDnsResolverPresentRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunGatewayCanBePingedRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runGatewayCanBePingedRoutine",
                             OS_DIAGNOSTICS_RUNGATEWAYCANBEPINGEDROUTINE)

  OsDiagnosticsRunGatewayCanBePingedRoutineFunction();
  OsDiagnosticsRunGatewayCanBePingedRoutineFunction(
      const OsDiagnosticsRunGatewayCanBePingedRoutineFunction&) = delete;
  OsDiagnosticsRunGatewayCanBePingedRoutineFunction& operator=(
      const OsDiagnosticsRunGatewayCanBePingedRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunGatewayCanBePingedRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunLanConnectivityRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runLanConnectivityRoutine",
                             OS_DIAGNOSTICS_RUNLANCONNECTIVITYROUTINE)

  OsDiagnosticsRunLanConnectivityRoutineFunction();
  OsDiagnosticsRunLanConnectivityRoutineFunction(
      const OsDiagnosticsRunLanConnectivityRoutineFunction&) = delete;
  OsDiagnosticsRunLanConnectivityRoutineFunction& operator=(
      const OsDiagnosticsRunLanConnectivityRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunLanConnectivityRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunMemoryRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runMemoryRoutine",
                             OS_DIAGNOSTICS_RUNMEMORYROUTINE)

  OsDiagnosticsRunMemoryRoutineFunction();
  OsDiagnosticsRunMemoryRoutineFunction(
      const OsDiagnosticsRunMemoryRoutineFunction&) = delete;
  OsDiagnosticsRunMemoryRoutineFunction& operator=(
      const OsDiagnosticsRunMemoryRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunMemoryRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunNvmeWearLevelRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runNvmeWearLevelRoutine",
                             OS_DIAGNOSTICS_RUNNVMEWEARLEVELROUTINE)

  OsDiagnosticsRunNvmeWearLevelRoutineFunction();
  OsDiagnosticsRunNvmeWearLevelRoutineFunction(
      const OsDiagnosticsRunNvmeWearLevelRoutineFunction&) = delete;
  OsDiagnosticsRunNvmeWearLevelRoutineFunction& operator=(
      const OsDiagnosticsRunNvmeWearLevelRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunNvmeWearLevelRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunSignalStrengthRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runSignalStrengthRoutine",
                             OS_DIAGNOSTICS_RUNSIGNALSTRENGTHROUTINE)

  OsDiagnosticsRunSignalStrengthRoutineFunction();
  OsDiagnosticsRunSignalStrengthRoutineFunction(
      const OsDiagnosticsRunSignalStrengthRoutineFunction&) = delete;
  OsDiagnosticsRunSignalStrengthRoutineFunction& operator=(
      const OsDiagnosticsRunSignalStrengthRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunSignalStrengthRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

class OsDiagnosticsRunSmartctlCheckRoutineFunction
    : public DiagnosticsApiRunRoutineFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("os.diagnostics.runSmartctlCheckRoutine",
                             OS_DIAGNOSTICS_RUNSMARTCTLCHECKROUTINE)

  OsDiagnosticsRunSmartctlCheckRoutineFunction();
  OsDiagnosticsRunSmartctlCheckRoutineFunction(
      const OsDiagnosticsRunSmartctlCheckRoutineFunction&) = delete;
  OsDiagnosticsRunSmartctlCheckRoutineFunction& operator=(
      const OsDiagnosticsRunSmartctlCheckRoutineFunction&) = delete;

 private:
  ~OsDiagnosticsRunSmartctlCheckRoutineFunction() override;

  // BaseTelemetryExtensionApiGuardFunction:
  void RunIfAllowed() override;
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_TELEMETRY_API_DIAGNOSTICS_API_H_
