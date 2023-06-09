// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_FEATURE_ENGAGEMENT_INTERNAL_CHROME_VARIATIONS_CONFIGURATION_H_
#define COMPONENTS_FEATURE_ENGAGEMENT_INTERNAL_CHROME_VARIATIONS_CONFIGURATION_H_

#include "base/feature_list.h"
#include "components/feature_engagement/public/configuration.h"
#include "components/feature_engagement/public/feature_list.h"

namespace feature_engagement {

// A ChromeVariationsConfiguration provides a configuration that is parsed from
// Chrome variations feature params. It is required to call
// ParseFeatureConfigs(...) with all the features that should be parsed.
class ChromeVariationsConfiguration : public Configuration {
 public:
  ChromeVariationsConfiguration();

  ChromeVariationsConfiguration(const ChromeVariationsConfiguration&) = delete;
  ChromeVariationsConfiguration& operator=(
      const ChromeVariationsConfiguration&) = delete;

  ~ChromeVariationsConfiguration() override;

  // Configuration implementation.
  const FeatureConfig& GetFeatureConfig(
      const base::Feature& feature) const override;
  const FeatureConfig& GetFeatureConfigByName(
      const std::string& feature_name) const override;
  const Configuration::ConfigMap& GetRegisteredFeatureConfigs() const override;
  const std::vector<std::string> GetRegisteredFeatures() const override;

  // Parses the variations configuration for all of the given |features| and
  // stores the result. It is only valid to call ParseFeatureConfig once.
  void ParseFeatureConfigs(const FeatureVector& features);

 private:
  void ParseFeatureConfig(const base::Feature* feature,
                          const FeatureVector& all_features);
  // Returns true if FeatureConfig was found with a local hard coded
  // configuration.
  bool MaybeAddClientSideFeatureConfig(const base::Feature* feature);

  // The current configurations.
  ConfigMap configs_;
};

}  // namespace feature_engagement

#endif  // COMPONENTS_FEATURE_ENGAGEMENT_INTERNAL_CHROME_VARIATIONS_CONFIGURATION_H_
