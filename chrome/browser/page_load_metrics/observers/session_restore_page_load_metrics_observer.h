// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PAGE_LOAD_METRICS_OBSERVERS_SESSION_RESTORE_PAGE_LOAD_METRICS_OBSERVER_H_
#define CHROME_BROWSER_PAGE_LOAD_METRICS_OBSERVERS_SESSION_RESTORE_PAGE_LOAD_METRICS_OBSERVER_H_

#include "components/page_load_metrics/browser/page_load_metrics_observer.h"

namespace internal {

extern const char kHistogramSessionRestoreForegroundTabFirstPaint[];
extern const char kHistogramSessionRestoreForegroundTabFirstContentfulPaint[];
extern const char kHistogramSessionRestoreForegroundTabFirstMeaningfulPaint[];

}  // namespace internal

// Record page load metrics of foreground tabs during session restore. This
// observer observes foreground tabs created by session restores only. It will
// stop observing if the tab gets hidden, reloaded, or navigated away.
class SessionRestorePageLoadMetricsObserver
    : public page_load_metrics::PageLoadMetricsObserver {
 public:
  SessionRestorePageLoadMetricsObserver();

  SessionRestorePageLoadMetricsObserver(
      const SessionRestorePageLoadMetricsObserver&) = delete;
  SessionRestorePageLoadMetricsObserver& operator=(
      const SessionRestorePageLoadMetricsObserver&) = delete;

  // page_load_metrics::PageLoadMetricsObserver:
  ObservePolicy OnStart(content::NavigationHandle* navigation_handle,
                        const GURL& currently_committed_url,
                        bool started_in_foreground) override;
  ObservePolicy OnFencedFramesStart(
      content::NavigationHandle* navigation_handle,
      const GURL& currently_committed_url) override;
  ObservePolicy OnPrerenderStart(content::NavigationHandle* navigation_handle,
                                 const GURL& currently_committed_url) override;
  ObservePolicy OnCommit(content::NavigationHandle* navigation_handle) override;
  void OnFirstPaintInPage(
      const page_load_metrics::mojom::PageLoadTiming& timing) override;
  void OnFirstContentfulPaintInPage(
      const page_load_metrics::mojom::PageLoadTiming& timing) override;
  void OnFirstMeaningfulPaintInMainFrameDocument(
      const page_load_metrics::mojom::PageLoadTiming& timing) override;
};

#endif  // CHROME_BROWSER_PAGE_LOAD_METRICS_OBSERVERS_SESSION_RESTORE_PAGE_LOAD_METRICS_OBSERVER_H_
