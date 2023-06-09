// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_SELECTION_SEGMENT_RESULT_PROVIDER_H_
#define COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_SELECTION_SEGMENT_RESULT_PROVIDER_H_

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "components/segmentation_platform/internal/database/segment_info_database.h"
#include "components/segmentation_platform/internal/execution/execution_request.h"
#include "components/segmentation_platform/public/input_context.h"
#include "components/segmentation_platform/public/proto/segmentation_platform.pb.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace base {
class Clock;
}
namespace segmentation_platform {

class DefaultModelManager;
class ExecutionService;
class SignalStorageConfig;

// Used for retrieving the result of a particular model.
// Supports 3 use cases:
//  1. Fetching cached and valid results from the segment database.
//  2. Fallback to default model when cached results are missing. Executes the
//     default model and provides the result.
//  3. Execute the TFLite model and provide the result when `ignore_db_scores`
//     is set.
class SegmentResultProvider {
 public:
  SegmentResultProvider() = default;
  virtual ~SegmentResultProvider() = default;

  enum class ResultState {
    kUnknown = 0,
    kSuccessFromDatabase = 1,
    kSegmentNotAvailable = 2,
    kSignalsNotCollected = 3,
    kDatabaseScoreNotReady = 4,
    kDefaultModelSignalNotCollected = 5,
    kDefaultModelMetadataMissing = 6,
    kDefaultModelExecutionFailed = 7,
    kDefaultModelScoreUsed = 8,
    kTfliteModelExecutionFailed = 9,
    kTfliteModelScoreUsed = 10,
  };
  struct SegmentResult {
    explicit SegmentResult(ResultState state);
    SegmentResult(ResultState state,
                  float rank,
                  std::unique_ptr<ModelExecutionResult> execution_result);
    ~SegmentResult();
    SegmentResult(SegmentResult&) = delete;
    SegmentResult& operator=(SegmentResult&) = delete;

    ResultState state = ResultState::kUnknown;
    absl::optional<float> rank;

    // The execution result is only available when the model is executed.
    // TODO(ssid): Support storing inputs to disk if needed.
    std::unique_ptr<ModelExecutionResult> execution_result;
  };
  using SegmentResultCallback =
      base::OnceCallback<void(std::unique_ptr<SegmentResult>)>;

  // Creates the instance.
  static std::unique_ptr<SegmentResultProvider> Create(
      SegmentInfoDatabase* segment_info_database,
      SignalStorageConfig* signal_storage_config,
      DefaultModelManager* default_model_manager,
      ExecutionService* execution_service,
      base::Clock* clock,
      bool force_refresh_results);

  // Options for `GetSegmentResult()`.
  struct GetResultOptions {
    GetResultOptions();
    ~GetResultOptions();

    // The segment ID to fetch result for.
    SegmentId segment_id = SegmentId::OPTIMIZATION_TARGET_UNKNOWN;

    // The key is needed for computing segment rank from discrete mapping.
    std::string discrete_mapping_key;

    // Ignores model results stored in database and executes them to fetch
    // results. When set to false, the result could be from following:
    //  * Score cached in the database
    //  * Execution of default model when score is missing.
    // When set to true, the result could be from following:
    //  * Execution of TFLite model.
    //  * Fallback to default when model is missing.
    bool ignore_db_scores = false;

    // If `ignore_db_scores` is true and TFLite model is available, then write
    // the results to database. Used when user wants to rerun the database
    // model.
    // TODO(ssid): `callback` is sometimes not called if this field is set to
    // true. Fix execution scheduler to run callback always even if save to
    // database is true.
    // TODO(ssid): Consider moving this option out as a different SaveRequest
    // method in this class.
    bool save_results_to_db = false;

    // Callback to return the segment result.
    SegmentResultCallback callback;

    // Current context of the browser that is needed by feature processor for
    // some of the models.
    scoped_refptr<InputContext> input_context;
  };

  // Returns latest available score for the segment.
  virtual void GetSegmentResult(std::unique_ptr<GetResultOptions> options) = 0;
};

}  // namespace segmentation_platform

#endif  // COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_SELECTION_SEGMENT_RESULT_PROVIDER_H_
