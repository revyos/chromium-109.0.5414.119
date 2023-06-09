// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_ACCESSIBILITY_PLATFORM_INSPECT_AX_TRANSFORM_MAC_H_
#define UI_ACCESSIBILITY_PLATFORM_INSPECT_AX_TRANSFORM_MAC_H_

#import <Cocoa/Cocoa.h>

#include "base/values.h"
#include "ui/accessibility/ax_export.h"
#include "ui/accessibility/platform/ax_platform_node_delegate.h"
#include "ui/accessibility/platform/inspect/ax_tree_indexer_mac.h"

namespace ui {

// Returns the base::Value representation of the given NSObject.
AX_EXPORT base::Value AXNSObjectToBaseValue(id, const AXTreeIndexerMac*);

// Returns the base::Value representation of the given NSAccessibilityElement.
AX_EXPORT base::Value AXElementToBaseValue(id, const AXTreeIndexerMac*);

// Returns the base::Value representation of the given AXPosition.
base::Value AXPositionToBaseValue(const AXPlatformNodeDelegate::AXPosition&,
                                  const AXTreeIndexerMac*);

// Returns the base::Value representation of the given AXTextMarker.
base::Value AXTextMarkerToBaseValue(id, const AXTreeIndexerMac*);

// Returns the base::Value representation of the given AXTextMarkerRange.
base::Value AXTextMarkerRangeToBaseValue(id, const AXTreeIndexerMac*);

// Returns the base::Value representation of nil.
AX_EXPORT base::Value AXNilToBaseValue();

// Returns the base::Value::List representation of the given NSArray.
AX_EXPORT base::Value::List AXNSArrayToBaseValue(NSArray*,
                                                 const AXTreeIndexerMac*);

// Returns the base::Value::Dict representation of the given NSDictionary.
AX_EXPORT base::Value::Dict AXNSDictionaryToBaseValue(NSDictionary*,
                                                      const AXTreeIndexerMac*);

// Returns the base::Value::Dict representation of the given NSPoint.
AX_EXPORT base::Value::Dict AXNSPointToBaseValue(NSPoint);

// Returns the base::Value::Dict representation of the given NSSize.
AX_EXPORT base::Value::Dict AXNSSizeToBaseValue(NSSize);

// Returns the base::Value::Dict representation of the given NSRect.
AX_EXPORT base::Value::Dict AXNSRectToBaseValue(NSRect);

// Returns the base::Value::Dict representation of the given NSRange.
AX_EXPORT base::Value::Dict AXNSRangeToBaseValue(NSRange);

}  // namespace ui

#endif  // UI_ACCESSIBILITY_PLATFORM_INSPECT_AX_TRANSFORM_MAC_H_
