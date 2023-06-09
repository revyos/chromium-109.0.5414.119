// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ATTRIBUTION_REPORTING_ATTRIBUTION_HOST_H_
#define CONTENT_BROWSER_ATTRIBUTION_REPORTING_ATTRIBUTION_HOST_H_

#include <stdint.h>

#include "base/containers/flat_map.h"
#include "content/common/content_export.h"
#include "content/public/browser/render_frame_host_receiver_set.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "third_party/blink/public/mojom/conversions/conversions.mojom.h"

namespace url {
class Origin;
}  // namespace url

namespace content {

class WebContents;

// Class responsible for listening to conversion events originating from blink,
// and verifying that they are valid. Owned by the WebContents. Lifetime is
// bound to lifetime of the WebContents.
class CONTENT_EXPORT AttributionHost
    : public WebContentsObserver,
      public WebContentsUserData<AttributionHost>,
      public blink::mojom::ConversionHost {
 public:
  explicit AttributionHost(WebContents* web_contents);
  AttributionHost(const AttributionHost&) = delete;
  AttributionHost& operator=(const AttributionHost&) = delete;
  AttributionHost(AttributionHost&&) = delete;
  AttributionHost& operator=(AttributionHost&&) = delete;
  ~AttributionHost() override;

  static void BindReceiver(
      mojo::PendingAssociatedReceiver<blink::mojom::ConversionHost> receiver,
      RenderFrameHost* rfh);

 private:
  friend class AttributionHostTestPeer;
  friend class WebContentsUserData<AttributionHost>;

  // blink::mojom::ConversionHost:
  void RegisterDataHost(mojo::PendingReceiver<blink::mojom::AttributionDataHost>
                            data_host) override;
  void RegisterNavigationDataHost(
      mojo::PendingReceiver<blink::mojom::AttributionDataHost> data_host,
      const blink::AttributionSrcToken& attribution_src_token) override;

  // WebContentsObserver:
  void DidStartNavigation(NavigationHandle* navigation_handle) override;
  void DidRedirectNavigation(NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(NavigationHandle* navigation_handle) override;

  // Returns the top frame origin corresponding to the current target frame.
  // Returns nullptr and reports a bad message if the top frame origin is not
  // potentially trustworthy or the current target frame is not a secure
  // context.
  [[nodiscard]] const url::Origin* TopFrameOriginForSecureContext();

  // Notifies the `AttributionDataHostManager` that a navigation with an
  // associated `AttributionDataHost` failed, if necessary.
  void MaybeNotifyFailedSourceNavigation(NavigationHandle* navigation_handle);

  // Map which stores the top-frame origin an impression occurred on for all
  // navigations with an associated impression, keyed by navigation ID.
  // Initiator origins are stored at navigation start time to have the best
  // chance of catching the initiating frame before it has a chance to go away.
  // Storing the origins at navigation start also prevents cases where a frame
  // initiates a navigation for itself, causing the frame to be correct but not
  // representing the frame state at the time the navigation was initiated. They
  // are stored until DidFinishNavigation, when they can be matched up with an
  // impression.
  //
  // A flat_map is used as the number of ongoing impression navigations is
  // expected to be very small in a given WebContents.
  using NavigationSourceOriginMap = base::flat_map<int64_t, url::Origin>;
  NavigationSourceOriginMap navigation_source_origins_;

  RenderFrameHostReceiverSet<blink::mojom::ConversionHost> receivers_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace content

#endif  // CONTENT_BROWSER_ATTRIBUTION_REPORTING_ATTRIBUTION_HOST_H_
