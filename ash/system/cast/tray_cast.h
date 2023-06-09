// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_CAST_TRAY_CAST_H_
#define ASH_SYSTEM_CAST_TRAY_CAST_H_

#include <string>
#include <vector>

#include "ash/public/cpp/cast_config_controller.h"
#include "ash/system/tray/tray_detailed_view.h"

namespace ash {

// This view displays a list of cast receivers that can be clicked on and casted
// to. It is activated by clicking on the chevron inside of
// |CastSelectDefaultView|.
class CastDetailedView : public TrayDetailedView,
                         public CastConfigController::Observer {
 public:
  explicit CastDetailedView(DetailedViewDelegate* delegate);

  CastDetailedView(const CastDetailedView&) = delete;
  CastDetailedView& operator=(const CastDetailedView&) = delete;

  ~CastDetailedView() override;

  // CastConfigController::Observer:
  void OnDevicesUpdated(const std::vector<SinkAndRoute>& devices) override;

  // views::View:
  const char* GetClassName() const override;

  views::View* get_add_access_code_device_for_testing() {
    return add_access_code_device_;
  }

 private:
  void CreateItems();

  void UpdateReceiverListFromCachedData();

  // TrayDetailedView:
  void HandleViewClicked(views::View* view) override;

  // A mapping from the sink id to the receiver/activity data.
  std::map<std::string, SinkAndRoute> sinks_and_routes_;
  // A mapping from the view pointer to the associated activity sink id.
  std::map<views::View*, std::string> view_to_sink_map_;

  // Special list item that, if clicked, launches the access code casting dialog
  views::View* add_access_code_device_ = nullptr;
};

}  // namespace ash

#endif  // ASH_SYSTEM_CAST_TRAY_CAST_H_
