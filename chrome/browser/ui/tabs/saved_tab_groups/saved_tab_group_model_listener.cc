// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/tabs/saved_tab_groups/saved_tab_group_model_listener.h"

#include "base/containers/flat_set.h"
#include "base/memory/raw_ptr.h"
#include "base/ranges/algorithm.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/tabs/tab_group.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/saved_tab_groups/saved_tab_group_model.h"

SavedTabGroupModelListener::SavedTabGroupModelListener() = default;

SavedTabGroupModelListener::SavedTabGroupModelListener(
    SavedTabGroupModel* model,
    Profile* profile)
    : model_(model), profile_(profile) {
  DCHECK(model);
  DCHECK(profile);
  BrowserList::GetInstance()->AddObserver(this);
  for (Browser* browser : *BrowserList::GetInstance())
    OnBrowserAdded(browser);
}

SavedTabGroupModelListener::~SavedTabGroupModelListener() {
  BrowserList::GetInstance()->RemoveObserver(this);
  // Note: Can no longer call OnBrowserRemoved here because model_ is already
  // destroyed.
  for (Browser* browser : observed_browsers_)
    browser->tab_strip_model()->RemoveObserver(this);

  observed_browsers_.clear();
}

TabStripModel* SavedTabGroupModelListener::GetTabStripModelWithTabGroupId(
    tab_groups::TabGroupId group_id) {
  auto contains_tab_group = [&](TabStripModel* model) {
    return model->group_model()->ContainsTabGroup(group_id);
  };
  base::flat_set<raw_ptr<Browser>>::iterator it = base::ranges::find_if(
      observed_browsers_, contains_tab_group, &Browser::tab_strip_model);
  return it != observed_browsers_.end() ? it->get()->tab_strip_model()
                                        : nullptr;
}

void SavedTabGroupModelListener::OnBrowserAdded(Browser* browser) {
  if (profile_ != browser->profile())
    return;
  if (observed_browsers_.count(browser)) {
    // TODO(crbug.com/1345680): Investigate the root cause of duplicate calls.
    return;
  }
  observed_browsers_.insert(browser);
  browser->tab_strip_model()->AddObserver(this);
}

void SavedTabGroupModelListener::OnBrowserRemoved(Browser* browser) {
  if (profile_ != browser->profile())
    return;
  observed_browsers_.erase(browser);
  browser->tab_strip_model()->RemoveObserver(this);
}

void SavedTabGroupModelListener::OnTabGroupChanged(
    const TabGroupChange& change) {
  const TabStripModel* tab_strip_model = change.model;
  if (!model_->Contains(change.group))
    return;

  const TabGroup* group =
      tab_strip_model->group_model()->GetTabGroup(change.group);
  switch (change.type) {
    // Called when the tabs in the group changes.
    case TabGroupChange::kContentsChanged: {
      // TODO(dljames): kContentsChanged will update the urls associated with
      // the group stored in the model with TabGroupId change.group.
      NOTIMPLEMENTED();
      return;
    }
    // Called when a groups title or color changes
    case TabGroupChange::kVisualsChanged: {
      const tab_groups::TabGroupVisualData* visual_data = group->visual_data();
      model_->UpdateVisualData(change.group, visual_data);
      return;
    }
    // Called when the last tab in the groups is removed.
    case TabGroupChange::kClosed: {
      model_->OnGroupClosedInTabStrip(change.group);
      return;
    }
    // Created is ignored because we explicitly add the TabGroupId to the saved
    // tab group outside of the observer flow. kEditorOpened does not affect the
    // SavedTabGroup, and kMoved does not affect the order of the saved tab
    // groups.
    case TabGroupChange::kCreated:
    case TabGroupChange::kEditorOpened:
    case TabGroupChange::kMoved: {
      NOTIMPLEMENTED();
      return;
    }
  }
}
