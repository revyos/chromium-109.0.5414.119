// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SAVED_TAB_GROUPS_SAVED_TAB_GROUP_TAB_H_
#define COMPONENTS_SAVED_TAB_GROUPS_SAVED_TAB_GROUP_TAB_H_

#include <memory>
#include <string>

#include "base/guid.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "components/sync/protocol/saved_tab_group_specifics.pb.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"

class SavedTabGroup;

// A SavedTabGroupTab stores the url, title, and favicon of a tab.
class SavedTabGroupTab {
 public:
  SavedTabGroupTab(const GURL& url,
                   const std::u16string& title,
                   const base::GUID& group_guid,
                   SavedTabGroup* group = nullptr,
                   absl::optional<base::GUID> guid = absl::nullopt,
                   absl::optional<base::Time>
                       creation_time_windows_epoch_micros = absl::nullopt,
                   absl::optional<base::Time> update_time_windows_epoch_micros =
                       absl::nullopt,
                   absl::optional<gfx::Image> favicon = absl::nullopt);
  SavedTabGroupTab(const SavedTabGroupTab& other);
  ~SavedTabGroupTab();

  // Accessors.
  const base::GUID& guid() const { return guid_; }
  const base::GUID& group_guid() const { return group_guid_; }
  SavedTabGroup* saved_tab_group() const { return saved_tab_group_; }
  const GURL& url() const { return url_; }
  const std::u16string& title() const { return title_; }
  const absl::optional<gfx::Image>& favicon() const { return favicon_; }
  const base::Time& creation_time_windows_epoch_micros() const {
    return creation_time_windows_epoch_micros_;
  }
  const base::Time& update_time_windows_epoch_micros() const {
    return update_time_windows_epoch_micros_;
  }

  // Mutators.
  SavedTabGroupTab& SetSavedTabGroup(SavedTabGroup* saved_tab_group) {
    saved_tab_group_ = saved_tab_group;
    SetUpdateTimeWindowsEpochMicros(base::Time::Now());
    return *this;
  }
  SavedTabGroupTab& SetURL(GURL url) {
    url_ = url;
    SetUpdateTimeWindowsEpochMicros(base::Time::Now());
    return *this;
  }
  SavedTabGroupTab& SetTitle(std::u16string title) {
    title_ = title;
    SetUpdateTimeWindowsEpochMicros(base::Time::Now());
    return *this;
  }
  SavedTabGroupTab& SetFavicon(absl::optional<gfx::Image> favicon) {
    favicon_ = favicon;
    SetUpdateTimeWindowsEpochMicros(base::Time::Now());
    return *this;
  }
  SavedTabGroupTab& SetUpdateTimeWindowsEpochMicros(
      base::Time update_time_windows_epoch_micros) {
    update_time_windows_epoch_micros_ = update_time_windows_epoch_micros;
    return *this;
  }

  // Merges this tabs data with a specific from sync and returns the newly
  // merged specific. Side effect: Updates the values in the tab.
  std::unique_ptr<sync_pb::SavedTabGroupSpecifics> MergeTab(
      std::unique_ptr<sync_pb::SavedTabGroupSpecifics> sync_specific);

  // We should merge a tab if one of the following is true:
  // 1. The data from `sync_specific` has the most recent (larger) update time.
  // 2. The `sync_specific` has the oldest (smallest) creation time.
  bool ShouldMergeTab(sync_pb::SavedTabGroupSpecifics* sync_specific);

  // Converts a `SavedTabGroupSpecifics` retrieved from sync into a
  // `SavedTabGroupTab`.
  static SavedTabGroupTab FromSpecifics(
      const sync_pb::SavedTabGroupSpecifics& specific);

  // Converts this `SavedTabGroupTab` into a `SavedTabGroupSpecifics` for sync.
  std::unique_ptr<sync_pb::SavedTabGroupSpecifics> ToSpecifics() const;

 private:
  // The ID used to represent the tab in sync.
  base::GUID guid_;

  // The ID used to represent the tab in sync. This must not be null. It
  base::GUID group_guid_;

  // The Group which owns this tab, this can be null if sync hasn't sent the
  // group over yet.
  raw_ptr<SavedTabGroup> saved_tab_group_;

  // The link to navigate with.
  GURL url_;

  // The title of the website this url is associated with.
  std::u16string title_;

  // The favicon of the website this SavedTabGroupTab represents.
  absl::optional<gfx::Image> favicon_;

  // Timestamp for when the tab was created using windows epoch microseconds.
  base::Time creation_time_windows_epoch_micros_;

  // Timestamp for when the tab was last updated using windows epoch
  // microseconds.
  base::Time update_time_windows_epoch_micros_;
};

#endif  // COMPONENTS_SAVED_TAB_GROUPS_SAVED_TAB_GROUP_TAB_H_
