// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/optimization_guide/content/browser/page_content_annotations_service.h"

#include "base/barrier_closure.h"
#include "base/callback_helpers.h"
#include "base/containers/adapters.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros_local.h"
#include "base/rand_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/history/core/browser/history_service.h"
#include "components/leveldb_proto/public/proto_database_provider.h"
#include "components/optimization_guide/content/browser/page_content_annotations_validator.h"
#include "components/optimization_guide/core/entity_metadata.h"
#include "components/optimization_guide/core/local_page_entities_metadata_provider.h"
#include "components/optimization_guide/core/optimization_guide_enums.h"
#include "components/optimization_guide/core/optimization_guide_features.h"
#include "components/optimization_guide/core/optimization_guide_logger.h"
#include "components/optimization_guide/core/optimization_guide_model_provider.h"
#include "components/optimization_guide/core/optimization_guide_switches.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"

#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
#include "components/optimization_guide/content/browser/page_content_annotations_model_manager.h"
#endif

namespace optimization_guide {

namespace {

// Keep this in sync with the PageContentAnnotationsStorageType variant in
// ../optimization/histograms.xml.
std::string PageContentAnnotationsTypeToString(
    PageContentAnnotationsType annotation_type) {
  switch (annotation_type) {
    case PageContentAnnotationsType::kUnknown:
      return "Unknown";
    case PageContentAnnotationsType::kModelAnnotations:
      return "ModelAnnotations";
    case PageContentAnnotationsType::kRelatedSearches:
      return "RelatedSearches";
    case PageContentAnnotationsType::kSearchMetadata:
      return "SearchMetadata";
    case PageContentAnnotationsType::kRemoteMetdata:
      return "RemoteMetadata";
  }
}

void LogPageContentAnnotationsStorageStatus(
    PageContentAnnotationsStorageStatus status,
    PageContentAnnotationsType annotation_type) {
  DCHECK_NE(status, PageContentAnnotationsStorageStatus::kUnknown);
  DCHECK_NE(annotation_type, PageContentAnnotationsType::kUnknown);
  base::UmaHistogramEnumeration(
      "OptimizationGuide.PageContentAnnotationsService."
      "ContentAnnotationsStorageStatus",
      status);

  base::UmaHistogramEnumeration(
      "OptimizationGuide.PageContentAnnotationsService."
      "ContentAnnotationsStorageStatus." +
          PageContentAnnotationsTypeToString(annotation_type),
      status);
}

}  // namespace

PageContentAnnotationsService::PageContentAnnotationsService(
    const std::string& application_locale,
    OptimizationGuideModelProvider* optimization_guide_model_provider,
    history::HistoryService* history_service,
    leveldb_proto::ProtoDatabaseProvider* database_provider,
    const base::FilePath& database_dir,
    OptimizationGuideLogger* optimization_guide_logger,
    scoped_refptr<base::SequencedTaskRunner> background_task_runner)
    : min_page_category_score_to_persist_(
          features::GetMinimumPageCategoryScoreToPersist()),
      last_annotated_history_visits_(
          features::MaxContentAnnotationRequestsCached()),
      annotated_text_cache_(features::MaxVisitAnnotationCacheSize()),
      optimization_guide_logger_(optimization_guide_logger) {
  DCHECK(optimization_guide_model_provider);
  DCHECK(history_service);
  history_service_ = history_service;
#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
  model_manager_ = std::make_unique<PageContentAnnotationsModelManager>(
      optimization_guide_model_provider);
  annotator_ = model_manager_.get();

  if (features::ShouldExecutePageVisibilityModelOnPageContent(
          application_locale)) {
    model_manager_->RequestAndNotifyWhenModelAvailable(
        AnnotationType::kContentVisibility, base::DoNothing());
    annotation_types_to_execute_.push_back(AnnotationType::kContentVisibility);
  }
  if (features::ShouldExecutePageEntitiesModelOnPageContent(
          application_locale)) {
    model_manager_->RequestAndNotifyWhenModelAvailable(
        AnnotationType::kPageEntities, base::DoNothing());
    annotation_types_to_execute_.push_back(AnnotationType::kPageEntities);
  }
#endif

  if (features::UseLocalPageEntitiesMetadataProvider()) {
    local_page_entities_metadata_provider_ =
        std::make_unique<LocalPageEntitiesMetadataProvider>();
    local_page_entities_metadata_provider_->Initialize(
        database_provider, database_dir, background_task_runner);
  }

  validator_ =
      PageContentAnnotationsValidator::MaybeCreateAndStartTimer(annotator_);
}

PageContentAnnotationsService::~PageContentAnnotationsService() = default;

void PageContentAnnotationsService::Annotate(const HistoryVisit& visit) {
  if (last_annotated_history_visits_.Peek(visit) !=
      last_annotated_history_visits_.end()) {
    // We have already been requested to annotate this visit, so don't submit
    // for re-annotation.
    return;
  }
  last_annotated_history_visits_.Put(visit, true);

#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
  if (!visit.text_to_annotate)
    return;
  // Used for testing.
  LOCAL_HISTOGRAM_BOOLEAN(
      "PageContentAnnotations.AnnotateVisit.AnnotationRequested", true);

  auto it = annotated_text_cache_.Peek(*visit.text_to_annotate);
  if (it != annotated_text_cache_.end()) {
    // We have annotations the text for this visit, so return that immediately
    // rather than re-executing the model.
    //
    // TODO(crbug.com/1291275): If the model was updated, the cached value could
    // be stale so we should invalidate the cache on model updates.
    OnPageContentAnnotated(visit, it->second);
    base::UmaHistogramBoolean(
        "OptimizationGuide.PageContentAnnotations.AnnotateVisitResultCached",
        true);
    return;
  }
  if (switches::ShouldLogPageContentAnnotationsInput()) {
    LOG(ERROR) << "Adding annotation job: \n"
               << "URL: " << visit.url << "\n"
               << "Text: " << visit.text_to_annotate.value_or(std::string());
  }
  visits_to_annotate_.emplace_back(visit);

  base::UmaHistogramBoolean(
      "OptimizationGuide.PageContentAnnotations.AnnotateVisitResultCached",
      false);

  if (MaybeStartAnnotateVisitBatch())
    return;

  // Used for testing.
  LOCAL_HISTOGRAM_BOOLEAN(
      "PageContentAnnotations.AnnotateVisit.AnnotationRequestQueued", true);

  if (visits_to_annotate_.size() > features::AnnotateVisitBatchSize()) {
    // The queue is full and an batch annotation is actively being done so
    // we will remove the "oldest" visit.
    visits_to_annotate_.erase(visits_to_annotate_.begin());
    // Used for testing.
    LOCAL_HISTOGRAM_BOOLEAN(
        "PageContentAnnotations.AnnotateVisit.QueueFullVisitDropped", true);
  }
#endif
}

#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
bool PageContentAnnotationsService::MaybeStartAnnotateVisitBatch() {
  bool is_full_batch_available =
      visits_to_annotate_.size() >= features::AnnotateVisitBatchSize();
  bool batch_already_running = !current_visit_annotation_batch_.empty();

  if (is_full_batch_available && !batch_already_running) {
    // Used for testing.
    LOCAL_HISTOGRAM_BOOLEAN(
        "PageContentAnnotations.AnnotateVisit.BatchAnnotationStarted", true);
    current_visit_annotation_batch_ = std::move(visits_to_annotate_);
    AnnotateVisitBatch();

    return true;
  }
  return false;
}

void PageContentAnnotationsService::AnnotateVisitBatch() {
  DCHECK(!current_visit_annotation_batch_.empty());

  std::vector<std::string> inputs;
  for (const HistoryVisit& visit : current_visit_annotation_batch_) {
    DCHECK(visit.text_to_annotate);
    inputs.push_back(*visit.text_to_annotate);
  }

  std::unique_ptr<
      std::vector<absl::optional<history::VisitContentModelAnnotations>>>
      merged_annotation_outputs = std::make_unique<
          std::vector<absl::optional<history::VisitContentModelAnnotations>>>();
  merged_annotation_outputs->reserve(inputs.size());
  for (size_t i = 0; i < inputs.size(); i++) {
    merged_annotation_outputs->push_back(absl::nullopt);
  }

  std::vector<absl::optional<history::VisitContentModelAnnotations>>*
      merged_annotation_outputs_ptr = merged_annotation_outputs.get();

  base::RepeatingClosure barrier_closure = base::BarrierClosure(
      annotation_types_to_execute_.size(),
      base::BindOnce(&PageContentAnnotationsService::OnBatchVisitsAnnotated,
                     weak_ptr_factory_.GetWeakPtr(),
                     std::move(merged_annotation_outputs)));

  for (AnnotationType type : annotation_types_to_execute_) {
    annotator_->Annotate(
        base::BindOnce(
            &PageContentAnnotationsService::OnAnnotationBatchComplete, type,
            merged_annotation_outputs_ptr, barrier_closure),
        inputs, type);
  }
}

// static
void PageContentAnnotationsService::OnAnnotationBatchComplete(
    AnnotationType type,
    std::vector<absl::optional<history::VisitContentModelAnnotations>>*
        merge_to_output,
    base::OnceClosure signal_merge_complete_callback,
    const std::vector<BatchAnnotationResult>& batch_result) {
  DCHECK_EQ(merge_to_output->size(), batch_result.size());
  for (size_t i = 0; i < batch_result.size(); i++) {
    const BatchAnnotationResult result = batch_result[i];
    DCHECK_EQ(type, result.type());

    if (!result.HasOutputForType())
      continue;

    history::VisitContentModelAnnotations current_annotations;

    if (type == AnnotationType::kContentVisibility) {
      DCHECK(result.visibility_score());
      current_annotations.visibility_score = *result.visibility_score();
    }

    if (type == AnnotationType::kPageEntities) {
      DCHECK(result.entities());
      for (const ScoredEntityMetadata& scored_md : *result.entities()) {
        DCHECK(scored_md.score >= 0.0 && scored_md.score <= 1.0);
        history::VisitContentModelAnnotations::Category category(
            scored_md.metadata.entity_id,
            static_cast<int>(100 * scored_md.score));
        history::VisitContentModelAnnotations::MergeCategoryIntoVector(
            category, &current_annotations.entities);
      }
    }

    history::VisitContentModelAnnotations previous_annotations =
        merge_to_output->at(i).value_or(
            history::VisitContentModelAnnotations());
    current_annotations.MergeFrom(previous_annotations);

    merge_to_output->at(i) = current_annotations;
  }

  // This needs to be ran last because |merge_to_output| may be deleted when
  // run.
  std::move(signal_merge_complete_callback).Run();
}

void PageContentAnnotationsService::OnBatchVisitsAnnotated(
    std::unique_ptr<
        std::vector<absl::optional<history::VisitContentModelAnnotations>>>
        merged_annotation_outputs) {
  DCHECK_EQ(merged_annotation_outputs->size(),
            current_visit_annotation_batch_.size());
  for (size_t i = 0; i < merged_annotation_outputs->size(); i++) {
    OnPageContentAnnotated(current_visit_annotation_batch_[i],
                           merged_annotation_outputs->at(i));
  }

  current_visit_annotation_batch_.clear();
  MaybeStartAnnotateVisitBatch();
}
#endif

void PageContentAnnotationsService::OverridePageContentAnnotatorForTesting(
    PageContentAnnotator* annotator) {
  annotator_ = annotator;
}

void PageContentAnnotationsService::BatchAnnotate(
    BatchAnnotationCallback callback,
    const std::vector<std::string>& inputs,
    AnnotationType annotation_type) {
  if (!annotator_) {
    std::move(callback).Run(CreateEmptyBatchAnnotationResults(inputs));
    return;
  }
  annotator_->Annotate(std::move(callback), inputs, annotation_type);
}

absl::optional<ModelInfo> PageContentAnnotationsService::GetModelInfoForType(
    AnnotationType type) const {
#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
  DCHECK(annotator_);
  return annotator_->GetModelInfoForType(type);
#else
  return absl::nullopt;
#endif
}

void PageContentAnnotationsService::RequestAndNotifyWhenModelAvailable(
    AnnotationType type,
    base::OnceCallback<void(bool)> callback) {
#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
  DCHECK(annotator_);
  annotator_->RequestAndNotifyWhenModelAvailable(type, std::move(callback));
#else
  std::move(callback).Run(false);
#endif
}

void PageContentAnnotationsService::PersistSearchMetadata(
    const HistoryVisit& visit,
    const SearchMetadata& search_metadata) {
  QueryURL(visit,
           base::BindOnce(&history::HistoryService::AddSearchMetadataForVisit,
                          history_service_->AsWeakPtr(),
                          search_metadata.normalized_url,
                          search_metadata.search_terms),
           PageContentAnnotationsType::kSearchMetadata);
}

void PageContentAnnotationsService::ExtractRelatedSearches(
    const HistoryVisit& visit,
    content::WebContents* web_contents) {
  search_result_extractor_client_.RequestData(
      web_contents, {continuous_search::mojom::ResultType::kRelatedSearches},
      base::BindOnce(&PageContentAnnotationsService::OnRelatedSearchesExtracted,
                     weak_ptr_factory_.GetWeakPtr(), visit));
}

#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
void PageContentAnnotationsService::OnPageContentAnnotated(
    const HistoryVisit& visit,
    const absl::optional<history::VisitContentModelAnnotations>&
        content_annotations) {
  base::UmaHistogramBoolean(
      "OptimizationGuide.PageContentAnnotationsService.ContentAnnotated",
      content_annotations.has_value());
  if (!content_annotations)
    return;

  bool is_new_entry = false;
  if (annotated_text_cache_.Peek(*visit.text_to_annotate) ==
      annotated_text_cache_.end()) {
    is_new_entry = true;
    annotated_text_cache_.Put(*visit.text_to_annotate, *content_annotations);
  }

  if (is_new_entry) {
    for (const auto& entity : content_annotations->entities) {
      // Skip low weight entities.
      if (entity.weight < 50)
        continue;
      GetMetadataForEntityId(
          entity.id,
          base::BindOnce(
              &PageContentAnnotationsService::OnEntityMetadataRetrieved,
              weak_ptr_factory_.GetWeakPtr(), visit.url, entity.id,
              entity.weight));
    }
  }

  if (!features::ShouldWriteContentAnnotationsToHistoryService())
    return;

  QueryURL(visit,
           base::BindOnce(
               &history::HistoryService::AddContentModelAnnotationsForVisit,
               history_service_->AsWeakPtr(), *content_annotations),
           PageContentAnnotationsType::kModelAnnotations);
}
#endif

void PageContentAnnotationsService::OnRelatedSearchesExtracted(
    const HistoryVisit& visit,
    continuous_search::SearchResultExtractorClientStatus status,
    continuous_search::mojom::CategoryResultsPtr results) {
  const bool success =
      status == continuous_search::SearchResultExtractorClientStatus::kSuccess;
  base::UmaHistogramBoolean(
      "OptimizationGuide.PageContentAnnotationsService."
      "RelatedSearchesExtracted",
      success);

  if (!success) {
    return;
  }

  std::vector<std::string> related_searches;
  for (const auto& group : results->groups) {
    if (group->type != continuous_search::mojom::ResultType::kRelatedSearches) {
      continue;
    }
    std::transform(std::begin(group->results), std::end(group->results),
                   std::back_inserter(related_searches),
                   [](const continuous_search::mojom::SearchResultPtr& result) {
                     return base::UTF16ToUTF8(
                         base::CollapseWhitespace(result->title, true));
                   });
    break;
  }

  if (related_searches.empty()) {
    return;
  }

  if (!features::ShouldWriteContentAnnotationsToHistoryService()) {
    return;
  }

  QueryURL(visit,
           base::BindOnce(&history::HistoryService::AddRelatedSearchesForVisit,
                          history_service_->AsWeakPtr(), related_searches),
           PageContentAnnotationsType::kRelatedSearches);
}

void PageContentAnnotationsService::QueryURL(
    const HistoryVisit& visit,
    PersistAnnotationsCallback callback,
    PageContentAnnotationsType annotation_type) {
  history_service_->QueryURL(
      visit.url, /*want_visits=*/true,
      base::BindOnce(&PageContentAnnotationsService::OnURLQueried,
                     weak_ptr_factory_.GetWeakPtr(), visit, std::move(callback),
                     annotation_type),
      &history_service_task_tracker_);
}

void PageContentAnnotationsService::OnURLQueried(
    const HistoryVisit& visit,
    PersistAnnotationsCallback callback,
    PageContentAnnotationsType annotation_type,
    history::QueryURLResult url_result) {
  if (!url_result.success || url_result.visits.empty()) {
    LogPageContentAnnotationsStorageStatus(
        PageContentAnnotationsStorageStatus::kNoVisitsForUrl, annotation_type);
    return;
  }

  base::TimeDelta min_magnitude_between_visits = base::TimeDelta::Max();
  bool did_store_content_annotations = false;
  for (const auto& visit_for_url : base::Reversed(url_result.visits)) {
    if (visit.nav_entry_timestamp != visit_for_url.visit_time) {
      base::TimeDelta magnitude_between_visits =
          (visit.nav_entry_timestamp - visit_for_url.visit_time).magnitude();
      if (magnitude_between_visits < min_magnitude_between_visits) {
        min_magnitude_between_visits = magnitude_between_visits;
      }
      continue;
    }

    std::move(callback).Run(visit_for_url.visit_id);

    did_store_content_annotations = true;
    break;
  }
  LogPageContentAnnotationsStorageStatus(
      did_store_content_annotations ? kSuccess : kSpecificVisitForUrlNotFound,
      annotation_type);
  if (!did_store_content_annotations) {
    DCHECK_NE(min_magnitude_between_visits, base::TimeDelta::Max());
    base::UmaHistogramTimes(
        "OptimizationGuide.PageContentAnnotationsService."
        "ContentAnnotationsStorageMinMagnitudeForVisitNotFound",
        min_magnitude_between_visits);

    base::UmaHistogramTimes(
        "OptimizationGuide.PageContentAnnotationsService."
        "ContentAnnotationsStorageMinMagnitudeForVisitNotFound." +
            PageContentAnnotationsTypeToString(annotation_type),
        min_magnitude_between_visits);
  }
}

void PageContentAnnotationsService::GetMetadataForEntityId(
    const std::string& entity_id,
    EntityMetadataRetrievedCallback callback) {
  if (features::UseLocalPageEntitiesMetadataProvider()) {
    DCHECK(local_page_entities_metadata_provider_);
    local_page_entities_metadata_provider_->GetMetadataForEntityId(
        entity_id, std::move(callback));
    return;
  }

#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
  model_manager_->GetMetadataForEntityId(entity_id, std::move(callback));
#else
  std::move(callback).Run(absl::nullopt);
#endif
}

void PageContentAnnotationsService::PersistRemotePageMetadata(
    const HistoryVisit& visit,
    const proto::PageEntitiesMetadata& page_entities_metadata) {
  // Persist entities and categories to VisitContentModelAnnotations if that
  // feature is enabled.
  history::VisitContentModelAnnotations model_annotations;
  for (const auto& entity : page_entities_metadata.entities()) {
    if (entity.entity_id().empty()) {
      continue;
    }
    if (entity.score() < 0 || entity.score() > 100) {
      continue;
    }

    model_annotations.entities.emplace_back(entity.entity_id(), entity.score());
  }

  std::vector<history::VisitContentModelAnnotations::Category> categories;
  for (const auto& category : page_entities_metadata.categories()) {
    int category_score = static_cast<int>(100 * category.score());
    if (category_score < min_page_category_score_to_persist_) {
      continue;
    }
    model_annotations.categories.emplace_back(category.category_id(),
                                              category_score);
  }

  if (!model_annotations.entities.empty() ||
      !model_annotations.categories.empty()) {
    // Only persist if we have something to persist.
    QueryURL(visit,
             base::BindOnce(
                 &history::HistoryService::AddContentModelAnnotationsForVisit,
                 history_service_->AsWeakPtr(), model_annotations),
             // Even though we are persisting remote page entities, we store
             // these as an override to the model annotations.
             PageContentAnnotationsType::kModelAnnotations);
  }

  // Persist any other metadata to VisitContentAnnotations, if enabled.
  if (!page_entities_metadata.alternative_title().empty()) {
    QueryURL(visit,
             base::BindOnce(&history::HistoryService::AddPageMetadataForVisit,
                            history_service_->AsWeakPtr(),
                            page_entities_metadata.alternative_title()),
             PageContentAnnotationsType::kRemoteMetdata);
  }
}

void PageContentAnnotationsService::OnEntityMetadataRetrieved(
    const GURL& url,
    const std::string& entity_id,
    int weight,
    const absl::optional<EntityMetadata>& entity_metadata) {
  if (!entity_metadata.has_value())
    return;

  GURL::Replacements replacements;
  replacements.ClearQuery();
  replacements.ClearRef();

  for (const auto& collection : entity_metadata->collections) {
    PageEntityCollection page_entity_collection =
        GetPageEntityCollectionForString(collection);
    base::UmaHistogramEnumeration(
        "OptimizationGuide.PageContentAnnotations.EntityCollection_50",
        page_entity_collection);
  }

  if (optimization_guide_logger_) {
    OPTIMIZATION_GUIDE_LOGGER(
        optimization_guide_common::mojom::LogSource::PAGE_CONTENT_ANNOTATIONS,
        optimization_guide_logger_)
        << "Entities: Url=" << url.ReplaceComponents(replacements)
        << " Weight=" << base::NumberToString(weight) << ". "
        << entity_metadata->ToHumanReadableString();
  }
}

// static
HistoryVisit PageContentAnnotationsService::CreateHistoryVisitFromWebContents(
    content::WebContents* web_contents,
    int64_t navigation_id) {
  HistoryVisit visit(
      web_contents->GetController().GetLastCommittedEntry()->GetTimestamp(),
      web_contents->GetLastCommittedURL(), navigation_id);
  return visit;
}

HistoryVisit::HistoryVisit() = default;

HistoryVisit::HistoryVisit(base::Time nav_entry_timestamp,
                           GURL url,
                           int64_t navigation_id) {
  this->nav_entry_timestamp = nav_entry_timestamp;
  this->url = url;
  this->navigation_id = navigation_id;
}

HistoryVisit::~HistoryVisit() = default;
HistoryVisit::HistoryVisit(const HistoryVisit&) = default;

}  // namespace optimization_guide
