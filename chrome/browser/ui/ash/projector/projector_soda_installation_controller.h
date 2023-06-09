// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_PROJECTOR_PROJECTOR_SODA_INSTALLATION_CONTROLLER_H_
#define CHROME_BROWSER_UI_ASH_PROJECTOR_PROJECTOR_SODA_INSTALLATION_CONTROLLER_H_

#include <string>

#include "ash/public/cpp/locale_update_controller.h"
#include "base/scoped_observation.h"
#include "components/soda/soda_installer.h"

namespace ash {
class ProjectorAppClient;
class ProjectorController;
}  // namespace ash

namespace speech {
enum class LanguageCode;
}  // namespace speech

// Class owned by ProjectorAppClientImpl used to control the installation of
// SODA and the language pack requested by the user.
class ProjectorSodaInstallationController
    : public speech::SodaInstaller::Observer,
      public ash::LocaleChangeObserver {
 public:
  ProjectorSodaInstallationController(ash::ProjectorAppClient* app_client,
                                      ash::ProjectorController* controller);
  ProjectorSodaInstallationController(
      const ProjectorSodaInstallationController&) = delete;
  ProjectorSodaInstallationController& operator=(
      const ProjectorSodaInstallationController&) = delete;

  ~ProjectorSodaInstallationController() override;

  // Installs the SODA binary and the the corresponding language if it is not
  // present.
  void InstallSoda(const std::string& language);

  // Checks if the device is eligible to install SODA and language pack for the
  // `language` provided.
  bool ShouldDownloadSoda(speech::LanguageCode language) const;

  // Checks if SODA binary and the requested `language` is downloaded and
  // available on device.
  bool IsSodaAvailable(speech::LanguageCode language) const;

 protected:
  // speech::SodaInstaller::Observer:
  void OnSodaInstalled(speech::LanguageCode language_code) override;
  void OnSodaInstallError(speech::LanguageCode language_code,
                          speech::SodaInstaller::ErrorCode error_code) override;
  void OnSodaProgress(speech::LanguageCode language_code,
                      int progress) override;

  // ash::LocaleChangeObserver:
  void OnLocaleChanged() override;

  ash::ProjectorAppClient* const app_client_;
  ash::ProjectorController* const projector_controller_;

 private:
  base::ScopedObservation<speech::SodaInstaller,
                          speech::SodaInstaller::Observer>
      soda_installer_observation_{this};

  base::ScopedObservation<ash::LocaleUpdateController,
                          ash::LocaleChangeObserver>
      locale_change_observation_{this};
};

#endif  // CHROME_BROWSER_UI_ASH_PROJECTOR_PROJECTOR_SODA_INSTALLATION_CONTROLLER_H_
