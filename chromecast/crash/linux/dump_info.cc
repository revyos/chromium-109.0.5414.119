// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "chromecast/crash/linux/dump_info.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>

#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"

namespace chromecast {

namespace {

// "%Y-%m-%d %H:%M:%S";
const char kDumpTimeFormat[] = "%04d-%02d-%02d %02d:%02d:%02d";

const int kNumRequiredParams = 4;

const char kNameKey[] = "name";
const char kDumpTimeKey[] = "dump_time";
const char kDumpKey[] = "dump";
const char kUptimeKey[] = "uptime";
const char kLogfileKey[] = "logfile";
const char kAttachmentsKey[] = "attachments";
const char kSuffixKey[] = "suffix";
const char kPrevAppNameKey[] = "prev_app_name";
const char kCurAppNameKey[] = "cur_app_name";
const char kLastAppNameKey[] = "last_app_name";
const char kReleaseVersionKey[] = "release_version";
const char kBuildNumberKey[] = "build_number";
const char kReasonKey[] = "reason";
const char kStadiaSessionIdKey[] = "stadia_session_id";
const char kCrashProductNameKey[] = "crash_product_name";
const char kExecNameKey[] = "exec_name";
const char kSignatureKey[] = "signature";
const char kExtraInfoKey[] = "extra_info";

}  // namespace

DumpInfo::DumpInfo(const base::Value* entry) : valid_(ParseEntry(entry)) {}

DumpInfo::DumpInfo(const std::string& crashed_process_dump,
                   const std::string& crashed_process_logfile,
                   const base::Time& dump_time,
                   const MinidumpParams& params,
                   const std::vector<std::string>* attachments)
    : crashed_process_dump_(crashed_process_dump),
      logfile_(crashed_process_logfile),
      dump_time_(dump_time),
      params_(params),
      valid_(true) {
  if (attachments) {
    attachments_ = *attachments;
  }
}

DumpInfo::~DumpInfo() {}

base::Value DumpInfo::GetAsValue() const {
  base::Value::Dict result;

  base::Time::Exploded ex;
  dump_time_.LocalExplode(&ex);
  std::string dump_time =
      base::StringPrintf(kDumpTimeFormat, ex.year, ex.month, ex.day_of_month,
                         ex.hour, ex.minute, ex.second);
  result.Set(kDumpTimeKey, dump_time);

  result.Set(kDumpKey, crashed_process_dump_);
  std::string uptime = std::to_string(params_.process_uptime);
  result.Set(kUptimeKey, uptime);
  result.Set(kLogfileKey, logfile_);

  base::Value::List attachments_list;
  for (const auto& attachment : attachments_) {
    attachments_list.Append(attachment);
  }
  result.Set(kAttachmentsKey, std::move(attachments_list));
  result.Set(kSuffixKey, params_.suffix);
  result.Set(kPrevAppNameKey, params_.previous_app_name);
  result.Set(kCurAppNameKey, params_.current_app_name);
  result.Set(kLastAppNameKey, params_.last_app_name);
  result.Set(kReleaseVersionKey, params_.cast_release_version);
  result.Set(kBuildNumberKey, params_.cast_build_number);
  result.Set(kReasonKey, params_.reason);
  result.Set(kStadiaSessionIdKey, params_.stadia_session_id);
  result.Set(kExecNameKey, params_.exec_name);
  result.Set(kSignatureKey, params_.signature);
  result.Set(kExtraInfoKey, params_.extra_info);
  result.Set(kCrashProductNameKey, params_.crash_product_name);

  return base::Value(std::move(result));
}

bool DumpInfo::ParseEntry(const base::Value* entry) {
  valid_ = false;

  if (!entry)
    return false;

  const base::DictionaryValue* dict;
  if (!entry->GetAsDictionary(&dict))
    return false;

  // Extract required fields.
  std::string dump_time;
  if (!dict->GetString(kDumpTimeKey, &dump_time))
    return false;
  if (!SetDumpTimeFromString(dump_time))
    return false;

  if (!dict->GetString(kDumpKey, &crashed_process_dump_))
    return false;

  std::string uptime;
  if (!dict->GetString(kUptimeKey, &uptime))
    return false;
  errno = 0;
  params_.process_uptime = strtoull(uptime.c_str(), nullptr, 0);
  if (errno != 0)
    return false;

  if (!dict->GetString(kLogfileKey, &logfile_))
    return false;
  size_t num_params = kNumRequiredParams;

  // Extract all other optional fields.
  const base::ListValue* attachments_list;
  if (dict->GetList(kAttachmentsKey, &attachments_list)) {
    ++num_params;
    for (const auto& attachment : attachments_list->GetList()) {
      attachments_.push_back(attachment.GetString());
    }
  }

  std::string unused_process_name;
  if (dict->GetString(kNameKey, &unused_process_name))
    ++num_params;
  if (dict->GetString(kSuffixKey, &params_.suffix))
    ++num_params;
  if (dict->GetString(kPrevAppNameKey, &params_.previous_app_name))
    ++num_params;
  if (dict->GetString(kCurAppNameKey, &params_.current_app_name))
    ++num_params;
  if (dict->GetString(kLastAppNameKey, &params_.last_app_name))
    ++num_params;
  if (dict->GetString(kReleaseVersionKey, &params_.cast_release_version))
    ++num_params;
  if (dict->GetString(kBuildNumberKey, &params_.cast_build_number))
    ++num_params;
  if (dict->GetString(kReasonKey, &params_.reason))
    ++num_params;
  if (dict->GetString(kStadiaSessionIdKey, &params_.stadia_session_id))
    ++num_params;
  if (dict->GetString(kExecNameKey, &params_.exec_name))
    ++num_params;
  if (dict->GetString(kSignatureKey, &params_.signature))
    ++num_params;
  if (dict->GetString(kExtraInfoKey, &params_.extra_info))
    ++num_params;
  if (dict->GetString(kCrashProductNameKey, &params_.crash_product_name))
    ++num_params;

  // Disallow extraneous params
  if (dict->DictSize() != num_params)
    return false;

  valid_ = true;
  return true;
}

bool DumpInfo::SetDumpTimeFromString(const std::string& timestr) {
  base::Time::Exploded ex = {0};
  if (sscanf(timestr.c_str(), kDumpTimeFormat, &ex.year, &ex.month,
             &ex.day_of_month, &ex.hour, &ex.minute, &ex.second) < 6) {
    LOG(INFO) << "Failed to convert dump time invalid";
    return false;
  }

  return base::Time::FromLocalExploded(ex, &dump_time_);
}

}  // namespace chromecast
