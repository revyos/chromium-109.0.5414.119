// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SERVICES_AUCTION_WORKLET_BIDDER_WORKLET_H_
#define CONTENT_SERVICES_AUCTION_WORKLET_BIDDER_WORKLET_H_

#include <stdint.h>

#include <cmath>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/containers/flat_map.h"
#include "base/containers/unique_ptr_adapters.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "content/common/content_export.h"
#include "content/services/auction_worklet/auction_v8_helper.h"
#include "content/services/auction_worklet/public/mojom/auction_worklet_service.mojom.h"
#include "content/services/auction_worklet/public/mojom/bidder_worklet.mojom.h"
#include "content/services/auction_worklet/public/mojom/private_aggregation_request.mojom-forward.h"
#include "content/services/auction_worklet/trusted_signals.h"
#include "content/services/auction_worklet/trusted_signals_request_manager.h"
#include "content/services/auction_worklet/worklet_loader.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/struct_ptr.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/mojom/interest_group/interest_group_types.mojom-forward.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "v8/include/v8-persistent-handle.h"

namespace v8 {
class UnboundScript;
}  // namespace v8

namespace auction_worklet {

class ContextRecycler;

// Represents a bidder worklet for FLEDGE
// (https://github.com/WICG/turtledove/blob/main/FLEDGE.md). Loads and runs the
// bidder worklet's Javascript.
//
// Each worklet object can only be used to load and run a single script's
// generateBid() and (if the bid is won) reportWin() once.
//
// The BidderWorklet is non-threadsafe, and lives entirely on the main / user
// sequence. It has an internal V8State object that runs scripts, and is only
// used on the V8 sequence.
//
// TODO(mmenke): Make worklets reuseable. Allow a single BidderWorklet instance
// to both be used for two generateBid() calls for different interest groups
// with the same owner in the same auction, and to be used to bid for the same
// interest group in different auctions.
class CONTENT_EXPORT BidderWorklet : public mojom::BidderWorklet {
 public:
  // Deletes the worklet immediately and resets the BidderWorklet's Mojo pipe
  // with the provided description. See mojo::Receiver::ResetWithReason().
  using ClosePipeCallback =
      base::OnceCallback<void(const std::string& description)>;

  using PrivateAggregationRequests =
      std::vector<auction_worklet::mojom::PrivateAggregationRequestPtr>;

  // Starts loading the worklet script on construction, as well as the trusted
  // bidding data, if necessary. Will then call the script's generateBid()
  // function and invoke the callback with the results. Callback will always be
  // invoked asynchronously, once a bid has been generated or a fatal error has
  // occurred.
  //
  // Data is cached and will be reused by ReportWin().
  BidderWorklet(scoped_refptr<AuctionV8Helper> v8_helper,
                bool pause_for_debugger_on_start,
                mojo::PendingRemote<network::mojom::URLLoaderFactory>
                    pending_url_loader_factory,
                const GURL& script_source_url,
                const absl::optional<GURL>& bidding_wasm_helper_url,
                const absl::optional<GURL>& trusted_bidding_signals_url,
                const url::Origin& top_window_origin,
                absl::optional<uint16_t> experiment_group_id);
  explicit BidderWorklet(const BidderWorklet&) = delete;
  ~BidderWorklet() override;
  BidderWorklet& operator=(const BidderWorklet&) = delete;

  // Sets the callback to be invoked on errors which require closing the pipe.
  // Callback will also immediately delete `this`. Not an argument to
  // constructor because the Mojo ReceiverId needs to be bound to the callback,
  // but can only get that after creating the worklet. Must be called
  // immediately after creating a BidderWorklet.
  void set_close_pipe_callback(ClosePipeCallback close_pipe_callback) {
    close_pipe_callback_ = std::move(close_pipe_callback);
  }

  int context_group_id_for_testing() const;

  static bool IsKAnon(const mojom::BidderWorkletNonSharedParams*
                          bidder_worklet_non_shared_params,
                      const GURL& url);
  static bool IsKAnon(const mojom::BidderWorkletNonSharedParams*
                          bidder_worklet_non_shared_params,
                      const mojom::BidderWorkletBidPtr& bid);

  // mojom::BidderWorklet implementation:
  void GenerateBid(
      mojom::BidderWorkletNonSharedParamsPtr bidder_worklet_non_shared_params,
      mojom::KAnonymityBidMode kanon_mode,
      const url::Origin& interest_group_join_origin,
      const absl::optional<std::string>& auction_signals_json,
      const absl::optional<std::string>& per_buyer_signals_json,
      const absl::optional<base::TimeDelta> per_buyer_timeout,
      const url::Origin& browser_signal_seller_origin,
      const absl::optional<url::Origin>& browser_signal_top_level_seller_origin,
      mojom::BiddingBrowserSignalsPtr bidding_browser_signals,
      base::Time auction_start_time,
      uint64_t trace_id,
      mojo::PendingAssociatedRemote<mojom::GenerateBidClient>
          generate_bid_client) override;
  void SendPendingSignalsRequests() override;
  void ReportWin(
      const std::string& interest_group_name,
      const absl::optional<std::string>& auction_signals_json,
      const absl::optional<std::string>& per_buyer_signals_json,
      const std::string& seller_signals_json,
      const GURL& browser_signal_render_url,
      double browser_signal_bid,
      double browser_signal_highest_scoring_other_bid,
      bool browser_signal_made_highest_scoring_other_bid,
      const url::Origin& browser_signal_seller_origin,
      const absl::optional<url::Origin>& browser_signal_top_level_seller_origin,
      uint32_t bidding_signals_data_version,
      bool has_bidding_signals_data_version,
      uint64_t trace_id,
      ReportWinCallback report_win_callback) override;
  void ConnectDevToolsAgent(
      mojo::PendingAssociatedReceiver<blink::mojom::DevToolsAgent> agent)
      override;

 private:
  struct GenerateBidTask {
    GenerateBidTask();
    ~GenerateBidTask();

    base::CancelableTaskTracker::TaskId task_id =
        base::CancelableTaskTracker::kBadTaskId;

    mojom::BidderWorkletNonSharedParamsPtr bidder_worklet_non_shared_params;
    mojom::KAnonymityBidMode kanon_mode;
    url::Origin interest_group_join_origin;
    absl::optional<std::string> auction_signals_json;
    absl::optional<std::string> per_buyer_signals_json;
    absl::optional<base::TimeDelta> per_buyer_timeout;
    url::Origin browser_signal_seller_origin;
    absl::optional<url::Origin> browser_signal_top_level_seller_origin;
    mojom::BiddingBrowserSignalsPtr bidding_browser_signals;
    base::Time auction_start_time;
    uint64_t trace_id;

    // Set while loading is in progress.
    std::unique_ptr<TrustedSignalsRequestManager::Request>
        trusted_bidding_signals_request;
    // Results of loading trusted bidding signals.
    scoped_refptr<TrustedSignals::Result> trusted_bidding_signals_result;
    // Error message returned by attempt to load `trusted_bidding_signals_`.
    // Errors loading it are not fatal, so such errors are cached here and only
    // reported on bid completion.
    absl::optional<std::string> trusted_bidding_signals_error_msg;

    // Set to true once the callback sent to the OnBiddingSignalsReceived()
    // method of `generate_bid_client` has been invoked. the Javascript
    // generateBid() method will not be run until that happens.
    bool signals_received_callback_invoked = false;

    mojo::AssociatedRemote<mojom::GenerateBidClient> generate_bid_client;
  };

  using GenerateBidTaskList = std::list<GenerateBidTask>;

  struct ReportWinTask {
    ReportWinTask();
    ~ReportWinTask();

    std::string interest_group_name;
    absl::optional<std::string> auction_signals_json;
    absl::optional<std::string> per_buyer_signals_json;
    std::string seller_signals_json;
    GURL browser_signal_render_url;
    double browser_signal_bid;
    double browser_signal_highest_scoring_other_bid;
    bool browser_signal_made_highest_scoring_other_bid;
    url::Origin browser_signal_seller_origin;
    absl::optional<url::Origin> browser_signal_top_level_seller_origin;
    absl::optional<uint32_t> bidding_signals_data_version;
    uint64_t trace_id;

    ReportWinCallback callback;
  };

  using ReportWinTaskList = std::list<ReportWinTask>;

  // Portion of BidderWorklet that deals with V8 execution, and therefore lives
  // on the v8 thread --- everything except the constructor must be run there.
  class V8State {
   public:
    V8State(scoped_refptr<AuctionV8Helper> v8_helper,
            scoped_refptr<AuctionV8Helper::DebugId> debug_id,
            const GURL& script_source_url,
            const url::Origin& top_window_origin,
            const absl::optional<GURL>& wasm_helper_url,
            const absl::optional<GURL>& trusted_bidding_signals_url,
            base::WeakPtr<BidderWorklet> parent);

    void SetWorkletScript(WorkletLoader::Result worklet_script);
    void SetWasmHelper(WorkletWasmLoader::Result wasm_helper);

    // These match the mojom GenerateBidCallback / ReportWinCallback functions,
    // except the errors vectors are passed by value. They're callbacks that
    // must be invoked on the main sequence, and passed to the V8State.
    using GenerateBidCallbackInternal = base::OnceCallback<void(
        mojom::BidderWorkletBidPtr bid,
        mojom::BidderWorkletKAnonEnforcedBidPtr kanon_bid,
        absl::optional<uint32_t> bidding_signals_data_version,
        absl::optional<GURL> debug_loss_report_url,
        absl::optional<GURL> debug_win_report_url,
        absl::optional<double> set_priority,
        base::flat_map<std::string, mojom::PrioritySignalsDoublePtr>
            update_priority_signals_overrides,
        PrivateAggregationRequests pa_requests,
        std::vector<std::string> error_msgs)>;
    using ReportWinCallbackInternal =
        base::OnceCallback<void(absl::optional<GURL> report_url,
                                base::flat_map<std::string, GURL> ad_beacon_map,
                                PrivateAggregationRequests pa_requests,
                                std::vector<std::string> errors)>;

    // Matches GenerateBidCallbackInternal, but with only one
    // BidderWorkletBidPtr.
    struct SingleGenerateBidResult {
      SingleGenerateBidResult();
      SingleGenerateBidResult(
          mojom::BidderWorkletBidPtr bid,
          absl::optional<uint32_t> bidding_signals_data_version,
          absl::optional<GURL> debug_loss_report_url,
          absl::optional<GURL> debug_win_report_url,
          absl::optional<double> set_priority,
          base::flat_map<std::string, mojom::PrioritySignalsDoublePtr>
              update_priority_signals_overrides,
          PrivateAggregationRequests pa_requests,
          std::vector<std::string> error_msgs);

      SingleGenerateBidResult(const SingleGenerateBidResult&) = delete;
      SingleGenerateBidResult(SingleGenerateBidResult&&);

      ~SingleGenerateBidResult();
      SingleGenerateBidResult& operator=(const SingleGenerateBidResult&) =
          delete;
      SingleGenerateBidResult& operator=(SingleGenerateBidResult&&);

      mojom::BidderWorkletBidPtr bid;
      absl::optional<uint32_t> bidding_signals_data_version;
      absl::optional<GURL> debug_loss_report_url;
      absl::optional<GURL> debug_win_report_url;
      absl::optional<double> set_priority;
      base::flat_map<std::string, mojom::PrioritySignalsDoublePtr>
          update_priority_signals_overrides;
      PrivateAggregationRequests pa_requests;
      std::vector<std::string> error_msgs;
    };

    void ReportWin(const std::string& interest_group_name,
                   const absl::optional<std::string>& auction_signals_json,
                   const absl::optional<std::string>& per_buyer_signals_json,
                   const std::string& seller_signals_json,
                   const GURL& browser_signal_render_url,
                   double browser_signal_bid,
                   double browser_signal_highest_scoring_other_bid,
                   bool browser_signal_made_highest_scoring_other_bid,
                   const url::Origin& browser_signal_seller_origin,
                   const absl::optional<url::Origin>&
                       browser_signal_top_level_seller_origin,
                   const absl::optional<uint32_t>& bidding_signals_data_version,
                   uint64_t trace_id,
                   ReportWinCallbackInternal callback);

    void GenerateBid(
        mojom::BidderWorkletNonSharedParamsPtr bidder_worklet_non_shared_params,
        mojom::KAnonymityBidMode kanon_mode,
        const url::Origin& interest_group_join_origin,
        const absl::optional<std::string>& auction_signals_json,
        const absl::optional<std::string>& per_buyer_signals_json,
        const absl::optional<base::TimeDelta> per_buyer_timeout,
        const url::Origin& browser_signal_seller_origin,
        const absl::optional<url::Origin>&
            browser_signal_top_level_seller_origin,
        mojom::BiddingBrowserSignalsPtr bidding_browser_signals,
        base::Time auction_start_time,
        scoped_refptr<TrustedSignals::Result> trusted_bidding_signals_result,
        uint64_t trace_id,
        base::ScopedClosureRunner cleanup_generate_bid_task,
        GenerateBidCallbackInternal callback);

    void ConnectDevToolsAgent(
        mojo::PendingAssociatedReceiver<blink::mojom::DevToolsAgent> agent);

   private:
    friend class base::DeleteHelper<V8State>;
    ~V8State();

    // Returns nullopt on error.
    absl::optional<SingleGenerateBidResult> GenerateSingleBid(
        const mojom::BidderWorkletNonSharedParamsPtr&
            bidder_worklet_non_shared_params,
        const url::Origin& interest_group_join_origin,
        const std::string* auction_signals_json,
        const std::string* per_buyer_signals_json,
        const absl::optional<base::TimeDelta> per_buyer_timeout,
        const url::Origin& browser_signal_seller_origin,
        const url::Origin* browser_signal_top_level_seller_origin,
        const mojom::BiddingBrowserSignalsPtr& bidding_browser_signals,
        base::Time auction_start_time,
        const scoped_refptr<TrustedSignals::Result>&
            trusted_bidding_signals_result,
        uint64_t trace_id,
        bool restrict_to_kanon_ads);

    void FinishInit();

    void PostReportWinCallbackToUserThread(
        ReportWinCallbackInternal callback,
        const absl::optional<GURL>& report_url,
        base::flat_map<std::string, GURL> ad_beacon_map,
        PrivateAggregationRequests pa_requests,
        std::vector<std::string> errors);

    void PostErrorBidCallbackToUserThread(
        GenerateBidCallbackInternal callback,
        std::vector<std::string> error_msgs = std::vector<std::string>());

    static void PostResumeToUserThread(
        base::WeakPtr<BidderWorklet> parent,
        scoped_refptr<base::SequencedTaskRunner> user_thread);

    const scoped_refptr<AuctionV8Helper> v8_helper_;
    const scoped_refptr<AuctionV8Helper::DebugId> debug_id_;
    const base::WeakPtr<BidderWorklet> parent_;
    const scoped_refptr<base::SequencedTaskRunner> user_thread_;

    const url::Origin owner_;

    // Compiled script, not bound to any context. Can be repeatedly bound to
    // different context and executed, without persisting any state.
    v8::Global<v8::UnboundScript> worklet_script_;

    // Loaded WASM module. Can be used to create instances for each context.
    WorkletWasmLoader::Result wasm_helper_;

    const GURL script_source_url_;
    const url::Origin top_window_origin_;
    const absl::optional<GURL> wasm_helper_url_;
    const absl::optional<GURL> trusted_bidding_signals_url_;

    std::unique_ptr<ContextRecycler> context_recycler_for_origin_group_mode_;
    url::Origin join_origin_for_origin_group_mode_;

    SEQUENCE_CHECKER(v8_sequence_checker_);
  };

  void ResumeIfPaused();
  void Start();

  void OnScriptDownloaded(WorkletLoader::Result worklet_script,
                          absl::optional<std::string> error_msg);
  void OnWasmDownloaded(WorkletWasmLoader::Result worklet_script,
                        absl::optional<std::string> error_msg);
  void RunReadyTasks();

  void OnTrustedBiddingSignalsDownloaded(
      GenerateBidTaskList::iterator task,
      scoped_refptr<TrustedSignals::Result> result,
      absl::optional<std::string> error_msg);

  // Invoked when the GenerateBidClient associated with `task` is destroyed.
  // Cancels bid generation.
  void OnGenerateBidClientDestroyed(GenerateBidTaskList::iterator task);

  // Callback passed to mojom::GenerateBidClient::OnSignalsReceived. Sets
  // `task->signals_received_callback_invoked` to true, and invokes
  // GenerateBidIfReady().
  void SignalsReceivedCallback(GenerateBidTaskList::iterator task);

  // Checks if the script has been loaded successfully, and the
  // TrustedSignals load has finished (successfully or not). If so, calls
  // generateBid(), and invokes `load_script_and_generate_bid_callback_` with
  // the resulting bid, if any. May only be called once BidderWorklet has
  // successfully loaded.
  void GenerateBidIfReady(GenerateBidTaskList::iterator task);

  void RunReportWin(ReportWinTaskList::iterator task);

  // Invokes the `callback` of `task` with the provided values, and removes
  // `task` from `generate_bid_tasks_`.
  void DeliverBidCallbackOnUserThread(
      GenerateBidTaskList::iterator task,
      mojom::BidderWorkletBidPtr bid,
      mojom::BidderWorkletKAnonEnforcedBidPtr kanon_bid,
      absl::optional<uint32_t> bidding_signals_data_version,
      absl::optional<GURL> debug_loss_report_url,
      absl::optional<GURL> debug_win_report_url,
      absl::optional<double> set_priority,
      base::flat_map<std::string, mojom::PrioritySignalsDoublePtr>
          update_priority_signals_overrides,
      PrivateAggregationRequests pa_requests,
      std::vector<std::string> error_msgs);

  // Removes `task` from `generate_bid_tasks_` only. Used in case where the
  // V8 work for task was cancelled only (via the `cleanup_generate_bid_task`
  // parameter getting destroyed).
  void CleanUpBidTaskOnUserThread(GenerateBidTaskList::iterator task);

  // Invokes the `callback` of `task` with the provided values, and removes
  // `task` from `report_win_tasks_`.
  void DeliverReportWinOnUserThread(
      ReportWinTaskList::iterator task,
      absl::optional<GURL> report_url,
      base::flat_map<std::string, GURL> ad_beacon_map,
      PrivateAggregationRequests pa_requests,
      std::vector<std::string> errors);

  // Returns true if unpaused and the script and WASM helper (if needed) have
  // loaded.
  bool IsCodeReady() const;

  scoped_refptr<base::SequencedTaskRunner> v8_runner_;

  const scoped_refptr<AuctionV8Helper> v8_helper_;
  const scoped_refptr<AuctionV8Helper::DebugId> debug_id_;
  mojo::Remote<network::mojom::URLLoaderFactory> url_loader_factory_;

  bool paused_;

  // Values shared by all interest groups that the BidderWorklet can be used
  // for.
  const GURL script_source_url_;
  absl::optional<GURL> wasm_helper_url_;

  // Populated only if `this` was created with a non-null
  // `trusted_scoring_signals_url`.
  std::unique_ptr<TrustedSignalsRequestManager>
      trusted_signals_request_manager_;

  // Top window origin for the auctions sharing this BidderWorklet.
  const url::Origin top_window_origin_;

  // These are deleted once each resource is loaded.
  std::unique_ptr<WorkletLoader> worklet_loader_;
  std::unique_ptr<WorkletWasmLoader> wasm_loader_;

  // Lives on `v8_runner_`. Since it's deleted there via DeleteSoon, tasks can
  // be safely posted from main thread to it with an Unretained pointer.
  std::unique_ptr<V8State, base::OnTaskRunnerDeleter> v8_state_;

  // Pending calls to the corresponding Javascript methods. Only accessed on
  // main thread, but iterators to their elements are bound to callbacks passed
  // to the v8 thread, so these need to be std::lists rather than std::vectors.
  GenerateBidTaskList generate_bid_tasks_;
  ReportWinTaskList report_win_tasks_;

  ClosePipeCallback close_pipe_callback_;

  // Errors that occurred while loading the code, if any.
  std::vector<std::string> load_code_error_msgs_;

  base::CancelableTaskTracker cancelable_task_tracker_;

  SEQUENCE_CHECKER(user_sequence_checker_);

  // Used when posting callbacks back from V8State.
  base::WeakPtrFactory<BidderWorklet> weak_ptr_factory_{this};
};

}  // namespace auction_worklet

#endif  // CONTENT_SERVICES_AUCTION_WORKLET_BIDDER_WORKLET_H_
