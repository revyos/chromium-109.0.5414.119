// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/connectors/service_provider_config.h"

#include "base/files/file_path.h"
#include "base/json/json_reader.h"
#include "chrome/browser/enterprise/connectors/connectors_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace enterprise_connectors {

namespace {

constexpr size_t kMaxFileSize = 50 * 1024 * 1024;

}  // namespace

TEST(ServiceProviderConfigTest, Google) {
  const ServiceProviderConfig* config = GetServiceProviderConfig();
  ASSERT_TRUE(config->count("google"));
  ServiceProvider service_provider = config->at("google");

  ASSERT_TRUE(service_provider.analysis);
  ASSERT_TRUE(service_provider.reporting);
  ASSERT_FALSE(service_provider.file_system);

  ASSERT_EQ("https://safebrowsing.google.com/safebrowsing/uploads/scan",
            std::string(service_provider.analysis->url));
  ASSERT_TRUE(GURL(service_provider.analysis->url).is_valid());
  ASSERT_EQ("https://chromereporting-pa.googleapis.com/v1/events",
            std::string(service_provider.reporting->url));
  ASSERT_TRUE(GURL(service_provider.reporting->url).is_valid());

  // The Google service provider has 2 tags: malware and dlp.
  ASSERT_EQ(service_provider.analysis->supported_tags.size(), 2u);
  ASSERT_EQ(std::string(service_provider.analysis->supported_tags[0].name),
            "malware");
  ASSERT_EQ(service_provider.analysis->supported_tags[0].max_file_size,
            kMaxFileSize);
  ASSERT_EQ(std::string(service_provider.analysis->supported_tags[1].name),
            "dlp");
  ASSERT_EQ(service_provider.analysis->supported_tags[1].max_file_size,
            kMaxFileSize);
}

TEST(ServiceProviderConfigTest, LocalTest1) {
  const ServiceProviderConfig* config = GetServiceProviderConfig();
  ASSERT_TRUE(config->count("local_user_agent"));
  ServiceProvider service_provider = config->at("local_user_agent");

  ASSERT_TRUE(service_provider.analysis);
  ASSERT_FALSE(service_provider.reporting);
  ASSERT_FALSE(service_provider.file_system);

  ASSERT_FALSE(service_provider.analysis->url);
  ASSERT_TRUE(service_provider.analysis->local_path);
  ASSERT_EQ("path_user", std::string(service_provider.analysis->local_path));
  ASSERT_TRUE(service_provider.analysis->user_specific);

  // The test local service provider has 1 tag: dlp.
  ASSERT_EQ(service_provider.analysis->supported_tags.size(), 1u);
  ASSERT_EQ(std::string(service_provider.analysis->supported_tags[0].name),
            "dlp");
  ASSERT_EQ(service_provider.analysis->supported_tags[0].max_file_size,
            kMaxFileSize);
}

TEST(ServiceProviderConfigTest, LocalTest2) {
  const ServiceProviderConfig* config = GetServiceProviderConfig();
  ASSERT_TRUE(config->count("local_system_agent"));
  ServiceProvider service_provider = config->at("local_system_agent");

  ASSERT_TRUE(service_provider.analysis);
  ASSERT_FALSE(service_provider.reporting);
  ASSERT_FALSE(service_provider.file_system);

  ASSERT_FALSE(service_provider.analysis->url);
  ASSERT_TRUE(service_provider.analysis->local_path);
  ASSERT_EQ("path_system", std::string(service_provider.analysis->local_path));
  ASSERT_FALSE(service_provider.analysis->user_specific);

  // The test local service provider has 1 tag: dlp.
  ASSERT_EQ(service_provider.analysis->supported_tags.size(), 1u);
  ASSERT_EQ(std::string(service_provider.analysis->supported_tags[0].name),
            "dlp");
  ASSERT_EQ(service_provider.analysis->supported_tags[0].max_file_size,
            kMaxFileSize);
}

TEST(ServiceProviderConfigTest, Box) {
  const ServiceProviderConfig* config = GetServiceProviderConfig();
  ASSERT_TRUE(config->count("box"));
  ServiceProvider service_provider = config->at("box");

  ASSERT_FALSE(service_provider.analysis);
  ASSERT_FALSE(service_provider.reporting);
  ASSERT_TRUE(service_provider.file_system);

  ASSERT_EQ("https://box.com", std::string(service_provider.file_system->home));
  ASSERT_EQ("https://account.box.com/api/oauth2/authorize",
            std::string(service_provider.file_system->authorization_endpoint));
  ASSERT_EQ("https://api.box.com/oauth2/token",
            std::string(service_provider.file_system->token_endpoint));
  ASSERT_EQ(20u * 1024 * 1024, service_provider.file_system->max_direct_size);
  ASSERT_TRUE(service_provider.file_system->scopes.empty());
  ASSERT_EQ(2u, service_provider.file_system->disable.size());
  ASSERT_EQ("box.com",
            std::string(service_provider.file_system->disable.at(0)));
  ASSERT_EQ("boxcloud.com",
            std::string(service_provider.file_system->disable.at(1)));
}

}  // namespace enterprise_connectors
