// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/attribution_reporting/attribution_manager.h"

#include "base/check.h"
#include "content/browser/attribution_reporting/attribution_manager_impl.h"
#include "content/browser/storage_partition_impl.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/mojom/conversions/attribution_reporting.mojom.h"

namespace content {

// static
AttributionManager* AttributionManager::FromWebContents(
    WebContents* web_contents) {
  DCHECK(web_contents);
  return static_cast<StoragePartitionImpl*>(
             web_contents->GetBrowserContext()->GetDefaultStoragePartition())
      ->GetAttributionManager();
}

// static
blink::mojom::AttributionOsSupport AttributionManager::GetOsSupport() {
  return AttributionManagerImpl::GetOsSupport();
}

}  // namespace content
