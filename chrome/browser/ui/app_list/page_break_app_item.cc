// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/page_break_app_item.h"

#include "chrome/browser/ui/app_list/page_break_constants.h"

// static
const char PageBreakAppItem::kItemType[] = "DefaultPageBreak";

PageBreakAppItem::PageBreakAppItem(
    Profile* profile,
    AppListModelUpdater* model_updater,
    const app_list::AppListSyncableService::SyncItem* sync_item,
    const std::string& app_id)
    : ChromeAppListItem(profile, app_id) {
  SetIsPageBreak(true);
  if (app_list::IsDefaultPageBreakItem(app_id))
    SetName("__default_page_break__");

  if (sync_item) {
    DCHECK_EQ(sync_item->item_type, sync_pb::AppListSpecifics::TYPE_PAGE_BREAK);
    if (sync_item->item_ordinal.IsValid()) {
      InitFromSync(sync_item);
      return;
    }
  }

  SetPosition(CalculateDefaultPositionIfApplicable());

  // Set model updater last to avoid being called during construction.
  set_model_updater(model_updater);
}

PageBreakAppItem::~PageBreakAppItem() = default;

// ChromeAppListItem:
void PageBreakAppItem::Activate(int event_flags) {
  NOTREACHED();
}

const char* PageBreakAppItem::GetItemType() const {
  return kItemType;
}
