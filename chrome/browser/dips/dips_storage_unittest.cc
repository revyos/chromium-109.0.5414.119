// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/dips/dips_storage.h"

#include "base/functional/bind.h"
#include "base/task/thread_pool.h"
#include "base/test/task_environment.h"
#include "base/threading/sequence_bound.h"
#include "chrome/browser/dips/dips_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "url/gurl.h"

namespace {

class TestStorage : public DIPSStorage {
 public:
  TestStorage() : DIPSStorage(absl::nullopt) {}

  void WriteForTesting(GURL url, const StateValue& state) {
    Write(DIPSState(this, GetSiteForDIPS(url), state));
  }
};

scoped_refptr<base::SequencedTaskRunner> CreateTaskRunner() {
  return base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
       base::ThreadPolicy::PREFER_BACKGROUND});
}

void StoreState(absl::optional<StateValue>* state_value,
                const DIPSState& state) {
  *state_value = state.was_loaded() ? absl::make_optional(state.ToStateValue())
                                    : absl::nullopt;
}

}  // namespace

class DIPSStorageTest : public testing::Test {
 public:
  DIPSStorageTest() = default;

 protected:
  TestStorage storage_;
};

TEST(DirtyBit, Constructor) {
  ASSERT_FALSE(DirtyBit());
  ASSERT_TRUE(DirtyBit(true));
  ASSERT_FALSE(DirtyBit(false));
}

TEST(DirtyBit, Assignment) {
  DirtyBit bit;

  bit = true;
  ASSERT_TRUE(bit);

  bit = false;
  ASSERT_FALSE(bit);
}

TEST(DirtyBit, Move) {
  DirtyBit bit(true);
  DirtyBit moved(std::move(bit));

  ASSERT_TRUE(moved);
  ASSERT_FALSE(bit);  // NOLINT
}

TEST(DIPSUtilsTest, GetSiteForDIPS) {
  EXPECT_EQ("example.com", GetSiteForDIPS(GURL("http://example.com/foo")));
  EXPECT_EQ("example.com", GetSiteForDIPS(GURL("https://www.example.com/bar")));
  EXPECT_EQ("example.com",
            GetSiteForDIPS(GURL("http://other.example.com/baz")));
  EXPECT_EQ("bar.baz.r.appspot.com",
            GetSiteForDIPS(GURL("http://foo.bar.baz.r.appspot.com/baz")));
  EXPECT_EQ("localhost", GetSiteForDIPS(GURL("http://localhost:8000/qux")));
  EXPECT_EQ("127.0.0.1", GetSiteForDIPS(GURL("http://127.0.0.1:8888/")));
  EXPECT_EQ("[::1]", GetSiteForDIPS(GURL("http://[::1]/")));
}

TEST_F(DIPSStorageTest, NewURL) {
  DIPSState state = storage_.Read(GURL("http://example.com/"));
  EXPECT_FALSE(state.was_loaded());
  EXPECT_FALSE(state.site_storage_times().first.has_value());
  EXPECT_FALSE(state.user_interaction_times().first.has_value());
}

TEST_F(DIPSStorageTest, SetValues) {
  GURL url("https://example.com");
  auto time1 = base::Time::FromDoubleT(1);
  auto time2 = base::Time::FromDoubleT(2);

  {
    DIPSState state = storage_.Read(url);
    state.update_site_storage_time(time1);
    state.update_user_interaction_time(time2);

    // Before flushing `state`, reads for the same URL won't include its
    // changes.
    DIPSState state2 = storage_.Read(url);
    EXPECT_FALSE(state2.site_storage_times().first.has_value());
    EXPECT_FALSE(state2.user_interaction_times().first.has_value());
  }

  DIPSState state = storage_.Read(url);
  EXPECT_TRUE(state.was_loaded());
  EXPECT_EQ(state.site_storage_times().first, absl::make_optional(time1));
  EXPECT_EQ(state.user_interaction_times().first, absl::make_optional(time2));
}

TEST_F(DIPSStorageTest, SameSiteSameState) {
  // The two urls use different subdomains of example.com; and one is HTTPS
  // while the other is HTTP.
  GURL url1("https://subdomain1.example.com");
  GURL url2("http://subdomain2.example.com");
  auto time = base::Time::FromDoubleT(1);

  storage_.Read(url1).update_site_storage_time(time);

  DIPSState state = storage_.Read(url2);
  // State was recorded for url1, but can be read for url2.
  EXPECT_EQ(time, state.site_storage_times().first);
  EXPECT_FALSE(state.user_interaction_times().first.has_value());
}

TEST_F(DIPSStorageTest, DifferentSiteDifferentState) {
  GURL url1("https://example1.com");
  GURL url2("https://example2.com");
  auto time1 = base::Time::FromDoubleT(1);
  auto time2 = base::Time::FromDoubleT(2);

  storage_.Read(url1).update_site_storage_time(time1);
  storage_.Read(url2).update_site_storage_time(time2);

  // Verify that url1 and url2 have independent state:
  EXPECT_EQ(storage_.Read(url1).site_storage_times().first,
            absl::make_optional(time1));
  EXPECT_EQ(storage_.Read(url2).site_storage_times().first,
            absl::make_optional(time2));
}

TEST_F(DIPSStorageTest, RemoveByTimeWithNullRangeEndTime) {
  GURL url1("https://example1.com");
  GURL url2("https://example2.com");
  base::Time delete_begin = base::Time::FromDoubleT(2);
  base::Time delete_end = base::Time();

  storage_.WriteForTesting(
      url1, {{base::Time::FromDoubleT(1), base::Time::FromDoubleT(3)},
             {base::Time::FromDoubleT(5), base::Time::FromDoubleT(8)}});
  storage_.WriteForTesting(
      url2, {{absl::nullopt, absl::nullopt},
             {base::Time::FromDoubleT(3), base::Time::FromDoubleT(5)}});
  storage_.RemoveEvents(delete_begin, delete_end,
                        base::RepeatingCallback<bool(const GURL&)>(),
                        DIPSEventRemovalType::kAll);

  DIPSState state1 = storage_.Read(url1);
  EXPECT_EQ(state1.site_storage_times().first,
            absl::make_optional(base::Time::FromDoubleT(1)));  // no change
  EXPECT_EQ(state1.site_storage_times().last,
            absl::make_optional(delete_begin));  // adjusted
  EXPECT_EQ(state1.user_interaction_times().first,
            absl::nullopt);  // removed
  EXPECT_EQ(state1.user_interaction_times().last,
            absl::nullopt);  // removed

  DIPSState state2 = storage_.Read(url2);
  EXPECT_FALSE(state2.was_loaded());  // removed
}

TEST_F(DIPSStorageTest, RemoveByTimeAdjustsOverlappingTimes) {
  GURL url1("https://example1.com");
  GURL url2("https://example2.com");
  base::Time delete_begin = base::Time::FromDoubleT(2);
  base::Time delete_end = base::Time::FromDoubleT(6);

  storage_.WriteForTesting(
      url1, {{base::Time::FromDoubleT(1), base::Time::FromDoubleT(3)},
             {base::Time::FromDoubleT(5), base::Time::FromDoubleT(8)}});
  storage_.WriteForTesting(
      url2, {{absl::nullopt, absl::nullopt},
             {base::Time::FromDoubleT(3), base::Time::FromDoubleT(5)}});
  storage_.RemoveEvents(delete_begin, delete_end,
                        base::RepeatingCallback<bool(const GURL&)>(),
                        DIPSEventRemovalType::kAll);

  DIPSState state1 = storage_.Read(url1);
  EXPECT_EQ(state1.site_storage_times().first,
            absl::make_optional(base::Time::FromDoubleT(1)));  // no change
  EXPECT_EQ(state1.site_storage_times().last,
            absl::make_optional(delete_begin));  // adjusted
  EXPECT_EQ(state1.user_interaction_times().first,
            absl::make_optional(delete_end));  // adjusted
  EXPECT_EQ(state1.user_interaction_times().last,
            absl::make_optional(base::Time::FromDoubleT(8)));  // no change

  DIPSState state2 = storage_.Read(url2);
  EXPECT_FALSE(state2.was_loaded());  // removed
}

TEST_F(DIPSStorageTest, RemoveByTimeDoesNotAffectTouchingWindowEndpoints) {
  GURL url1("https://example1.com");
  GURL url2("https://example2.com");
  base::Time delete_begin = base::Time::FromDoubleT(3);
  base::Time delete_end = base::Time::FromDoubleT(5);

  storage_.WriteForTesting(
      url1, {{base::Time::FromDoubleT(1), base::Time::FromDoubleT(3)},
             {base::Time::FromDoubleT(5), base::Time::FromDoubleT(8)}});
  storage_.RemoveEvents(delete_begin, delete_end,
                        base::RepeatingCallback<bool(const GURL&)>(),
                        DIPSEventRemovalType::kAll);

  DIPSState state = storage_.Read(url1);
  EXPECT_EQ(state.site_storage_times().first,
            absl::make_optional(base::Time::FromDoubleT(1)));  // no change
  EXPECT_EQ(state.site_storage_times().last,
            absl::make_optional(base::Time::FromDoubleT(3)));  // no change
  EXPECT_EQ(state.user_interaction_times().first,
            absl::make_optional(base::Time::FromDoubleT(5)));  // no change
  EXPECT_EQ(state.user_interaction_times().last,
            absl::make_optional(base::Time::FromDoubleT(8)));  // no change
}

TEST_F(DIPSStorageTest, RemoveByTimeStorageOnly) {
  GURL url1("https://example1.com");
  GURL url2("https://example2.com");
  base::Time delete_begin = base::Time::FromDoubleT(2);
  base::Time delete_end = base::Time::FromDoubleT(6);

  storage_.WriteForTesting(
      url1, {{base::Time::FromDoubleT(1), base::Time::FromDoubleT(3)},
             {base::Time::FromDoubleT(5), base::Time::FromDoubleT(8)}});
  storage_.WriteForTesting(
      url2, {{absl::nullopt, absl::nullopt},
             {base::Time::FromDoubleT(3), base::Time::FromDoubleT(5)}});
  storage_.RemoveEvents(delete_begin, delete_end,
                        base::RepeatingCallback<bool(const GURL&)>(),
                        DIPSEventRemovalType::kStorage);

  DIPSState state1 = storage_.Read(url1);
  EXPECT_EQ(state1.site_storage_times().first,
            absl::make_optional(base::Time::FromDoubleT(1)));  // no change
  EXPECT_EQ(state1.site_storage_times().last,
            absl::make_optional(delete_begin));  // adjusted
  EXPECT_EQ(state1.user_interaction_times().first,
            absl::make_optional(base::Time::FromDoubleT(5)));  // no change
  EXPECT_EQ(state1.user_interaction_times().last,
            absl::make_optional(base::Time::FromDoubleT(8)));  // no change

  DIPSState state2 = storage_.Read(url2);
  EXPECT_EQ(state2.user_interaction_times().first,
            absl::make_optional(base::Time::FromDoubleT(3)));  // no change
  EXPECT_EQ(state2.user_interaction_times().last,
            absl::make_optional(base::Time::FromDoubleT(5)));  // no change
}

TEST_F(DIPSStorageTest, RemoveByTimeInteractionOnly) {
  GURL url1("https://example1.com");
  GURL url2("https://example2.com");
  base::Time delete_begin = base::Time::FromDoubleT(2);
  base::Time delete_end = base::Time::FromDoubleT(6);

  storage_.WriteForTesting(
      url1, {{base::Time::FromDoubleT(1), base::Time::FromDoubleT(3)},
             {base::Time::FromDoubleT(5), base::Time::FromDoubleT(8)}});
  storage_.WriteForTesting(
      url2, {{absl::nullopt, absl::nullopt},
             {base::Time::FromDoubleT(3), base::Time::FromDoubleT(5)}});
  storage_.RemoveEvents(delete_begin, delete_end,
                        base::RepeatingCallback<bool(const GURL&)>(),
                        DIPSEventRemovalType::kHistory);

  DIPSState state1 = storage_.Read(url1);
  EXPECT_EQ(state1.site_storage_times().first,
            absl::make_optional(base::Time::FromDoubleT(1)));  // no change
  EXPECT_EQ(state1.site_storage_times().last,
            absl::make_optional(base::Time::FromDoubleT(3)));  // no change
  EXPECT_EQ(state1.user_interaction_times().first,
            absl::make_optional(delete_end));  // adjusted
  EXPECT_EQ(state1.user_interaction_times().last,
            absl::make_optional(base::Time::FromDoubleT(8)));  // no change

  DIPSState state2 = storage_.Read(url2);
  EXPECT_FALSE(state2.was_loaded());  // removed
}

TEST_F(DIPSStorageTest, RemoveByTimeStatefulOnly) {
  GURL url1("https://example1.com");
  GURL url2("https://example2.com");
  base::Time delete_begin = base::Time::FromDoubleT(2);
  base::Time delete_end = base::Time::FromDoubleT(6);

  storage_.WriteForTesting(
      url1, {{absl::nullopt, absl::nullopt},
             {absl::nullopt, absl::nullopt},
             {base::Time::FromDoubleT(1), base::Time::FromDoubleT(3)},
             {base::Time::FromDoubleT(5), base::Time::FromDoubleT(8)}});
  storage_.WriteForTesting(
      url2, {{absl::nullopt, absl::nullopt},
             {absl::nullopt, absl::nullopt},
             {absl::nullopt, absl::nullopt},
             {base::Time::FromDoubleT(3), base::Time::FromDoubleT(5)}});
  storage_.RemoveEvents(delete_begin, delete_end,
                        base::RepeatingCallback<bool(const GURL&)>(),
                        DIPSEventRemovalType::kStorage);

  DIPSState state1 = storage_.Read(url1);
  EXPECT_EQ(state1.stateful_bounce_times().first,
            absl::make_optional(base::Time::FromDoubleT(1)));  // no change
  EXPECT_EQ(state1.stateful_bounce_times().last,
            absl::make_optional(delete_begin));  // adjusted
  EXPECT_EQ(state1.stateless_bounce_times().first,
            absl::make_optional(base::Time::FromDoubleT(5)));  // no change
  EXPECT_EQ(state1.stateless_bounce_times().last,
            absl::make_optional(base::Time::FromDoubleT(8)));  // no change

  DIPSState state2 = storage_.Read(url2);
  EXPECT_EQ(state2.stateless_bounce_times().first,
            absl::make_optional(base::Time::FromDoubleT(3)));  // no change
  EXPECT_EQ(state2.stateless_bounce_times().last,
            absl::make_optional(base::Time::FromDoubleT(5)));  // no change
}

TEST_F(DIPSStorageTest, RemoveByTimeStatelessOnly) {
  GURL url1("https://example1.com");
  GURL url2("https://example2.com");
  base::Time delete_begin = base::Time::FromDoubleT(2);
  base::Time delete_end = base::Time::FromDoubleT(6);

  storage_.WriteForTesting(
      url1, {{absl::nullopt, absl::nullopt},
             {absl::nullopt, absl::nullopt},
             {base::Time::FromDoubleT(1), base::Time::FromDoubleT(3)},
             {base::Time::FromDoubleT(5), base::Time::FromDoubleT(8)}});
  storage_.WriteForTesting(
      url2, {{absl::nullopt, absl::nullopt},
             {absl::nullopt, absl::nullopt},
             {absl::nullopt, absl::nullopt},
             {base::Time::FromDoubleT(3), base::Time::FromDoubleT(5)}});
  storage_.RemoveEvents(delete_begin, delete_end,
                        base::RepeatingCallback<bool(const GURL&)>(),
                        DIPSEventRemovalType::kHistory);

  DIPSState state1 = storage_.Read(url1);
  EXPECT_EQ(state1.stateful_bounce_times().first,
            absl::make_optional(base::Time::FromDoubleT(1)));  // no change
  EXPECT_EQ(state1.stateful_bounce_times().last,
            absl::make_optional(base::Time::FromDoubleT(3)));  // no change
  EXPECT_EQ(state1.stateless_bounce_times().first,
            absl::make_optional(delete_end));  // adjusted
  EXPECT_EQ(state1.stateless_bounce_times().last,
            absl::make_optional(base::Time::FromDoubleT(8)));  // no change

  DIPSState state2 = storage_.Read(url2);
  EXPECT_FALSE(state2.was_loaded());  // removed
}

class DIPSStoragePrepopulateTest : public testing::Test {
 public:
  DIPSStoragePrepopulateTest()
      : task_environment_(base::test::TaskEnvironment(
            base::test::TaskEnvironment::ThreadPoolExecutionMode::QUEUED)),
        storage_(base::SequenceBound<DIPSStorage>(CreateTaskRunner(),
                                                  absl::nullopt)) {}

 protected:
  base::test::TaskEnvironment task_environment_;
  base::SequenceBound<DIPSStorage> storage_;
};

TEST_F(DIPSStoragePrepopulateTest, NoExistingTime) {
  base::Time time = base::Time::FromDoubleT(1);

  storage_.AsyncCall(&DIPSStorage::Prepopulate)
      .WithArgs(time, std::vector<std::string>{"site"});
  absl::optional<StateValue> state;
  storage_.AsyncCall(&DIPSStorage::Read)
      .WithArgs(GURL("http://site"))
      .Then(base::BindOnce(StoreState, &state));
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(state->user_interaction_times.first, time);  // written
  EXPECT_EQ(state->site_storage_times.first, time);      // written
}

TEST_F(DIPSStoragePrepopulateTest, ExistingStorageAndInteractionTimes) {
  base::Time interaction_time = base::Time::FromDoubleT(1);
  base::Time storage_time = base::Time::FromDoubleT(2);
  base::Time prepopulate_time = base::Time::FromDoubleT(3);

  // First record interaction and storage for the site, then call Prepopulate().
  storage_.AsyncCall(&DIPSStorage::RecordInteraction)
      .WithArgs(GURL("http://site"), interaction_time,
                DIPSCookieMode::kStandard);
  storage_.AsyncCall(&DIPSStorage::RecordStorage)
      .WithArgs(GURL("http://site"), storage_time, DIPSCookieMode::kStandard);
  storage_.AsyncCall(&DIPSStorage::Prepopulate)
      .WithArgs(prepopulate_time, std::vector<std::string>{"site"});
  absl::optional<StateValue> state;
  storage_.AsyncCall(&DIPSStorage::Read)
      .WithArgs(GURL("http://site"))
      .Then(base::BindOnce(StoreState, &state));
  task_environment_.RunUntilIdle();

  // Prepopulate() didn't overwrite the previous timestamps.
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(state->user_interaction_times.first,
            interaction_time);  // no change
  EXPECT_EQ(state->site_storage_times.first,
            storage_time);  // no change
}

TEST_F(DIPSStoragePrepopulateTest, ExistingStorageTime) {
  base::Time storage_time = base::Time::FromDoubleT(1);
  base::Time prepopulate_time = base::Time::FromDoubleT(2);

  // Record only storage for the site, then call Prepopulate().
  storage_.AsyncCall(&DIPSStorage::RecordStorage)
      .WithArgs(GURL("http://site"), storage_time, DIPSCookieMode::kStandard);
  storage_.AsyncCall(&DIPSStorage::Prepopulate)
      .WithArgs(prepopulate_time, std::vector<std::string>{"site"});
  absl::optional<StateValue> state;
  storage_.AsyncCall(&DIPSStorage::Read)
      .WithArgs(GURL("http://site"))
      .Then(base::BindOnce(StoreState, &state));
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(state->site_storage_times.first,
            storage_time);  // no change
  EXPECT_EQ(state->user_interaction_times.first,
            prepopulate_time);  // written
}

TEST_F(DIPSStoragePrepopulateTest, ExistingInteractionTime) {
  base::Time interaction_time = base::Time::FromDoubleT(1);
  base::Time prepopulate_time = base::Time::FromDoubleT(2);

  // Record only storage for the site, then call Prepopulate().
  storage_.AsyncCall(&DIPSStorage::RecordInteraction)
      .WithArgs(GURL("http://site"), interaction_time,
                DIPSCookieMode::kStandard);
  storage_.AsyncCall(&DIPSStorage::Prepopulate)
      .WithArgs(prepopulate_time, std::vector<std::string>{"site"});
  absl::optional<StateValue> state;
  storage_.AsyncCall(&DIPSStorage::Read)
      .WithArgs(GURL("http://site"))
      .Then(base::BindOnce(StoreState, &state));
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(state->user_interaction_times.first,
            interaction_time);  // no change
  EXPECT_EQ(state->site_storage_times.first,
            absl::nullopt);  // no change
}

TEST_F(DIPSStoragePrepopulateTest, WorksOnChunks) {
  base::Time time = base::Time::FromDoubleT(1);
  std::vector<std::string> sites = {"site1", "site2", "site3"};
  DIPSStorage::SetPrepopulateChunkSizeForTesting(2);

  absl::optional<StateValue> state1, state2, state3;
  auto queue_state_reads = [&]() {
    storage_.AsyncCall(&DIPSStorage::Read)
        .WithArgs(GURL("http://site1"))
        .Then(base::BindOnce(StoreState, &state1));
    storage_.AsyncCall(&DIPSStorage::Read)
        .WithArgs(GURL("http://site2"))
        .Then(base::BindOnce(StoreState, &state2));
    storage_.AsyncCall(&DIPSStorage::Read)
        .WithArgs(GURL("http://site3"))
        .Then(base::BindOnce(StoreState, &state3));
  };

  storage_.AsyncCall(&DIPSStorage::Prepopulate)
      .WithArgs(time, std::move(sites));
  queue_state_reads();
  task_environment_.RunUntilIdle();

  // At this point, the entire |sites| vector has been processed. But we made
  // async calls to read the state for each site before Prepopulate()
  // actually ran, so the reads were performed after only the first chunk of
  // |sites| was processed.

  // The first two sites were prepopulated.
  EXPECT_TRUE(state1.has_value());
  EXPECT_TRUE(state2.has_value());
  // The last wasn't.
  ASSERT_FALSE(state3.has_value());

  queue_state_reads();
  task_environment_.RunUntilIdle();

  // Now we've read the final state for all sites.
  EXPECT_TRUE(state1.has_value());
  EXPECT_TRUE(state2.has_value());
  EXPECT_TRUE(state3.has_value());
}
