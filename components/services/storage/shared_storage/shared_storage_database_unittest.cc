// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/services/storage/shared_storage/shared_storage_database.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/simple_test_clock.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "components/services/storage/public/mojom/storage_usage_info.mojom.h"
#include "components/services/storage/shared_storage/shared_storage_options.h"
#include "components/services/storage/shared_storage/shared_storage_test_utils.h"
#include "sql/database.h"
#include "storage/browser/quota/special_storage_policy.h"
#include "storage/browser/test/mock_special_storage_policy.h"
#include "testing/gmock/include/gmock/gmock-matchers.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace storage {

namespace {

using ::testing::ElementsAre;
using StorageKeyPolicyMatcherFunction =
    SharedStorageDatabase::StorageKeyPolicyMatcherFunction;
using InitStatus = SharedStorageDatabase::InitStatus;
using SetBehavior = SharedStorageDatabase::SetBehavior;
using OperationResult = SharedStorageDatabase::OperationResult;
using GetResult = SharedStorageDatabase::GetResult;
using TimeResult = SharedStorageDatabase::TimeResult;

const int kBudgetIntervalHours = 24;
const int kOriginStalenessThresholdDays = 1;
const int kBitBudget = 8;
const int kMaxEntriesPerOrigin = 5;
const int kMaxEntriesPerOriginForIteratorTest = 1000;
const int kMaxStringLength = 100;
const int kMaxBatchSizeForIteratorTest = 25;

constexpr char kFileSizeKBHistogram[] =
    "Storage.SharedStorage.Database.FileBacked.FileSize.KB";
constexpr char kNumEntriesMaxHistogram[] =
    "Storage.SharedStorage.Database.FileBacked.NumEntries.PerOrigin.Max";
constexpr char kNumEntriesMinHistogram[] =
    "Storage.SharedStorage.Database.FileBacked.NumEntries.PerOrigin.Min";
constexpr char kNumEntriesMedianHistogram[] =
    "Storage.SharedStorage.Database.FileBacked.NumEntries.PerOrigin.Median";
constexpr char kNumEntriesQ1Histogram[] =
    "Storage.SharedStorage.Database.FileBacked.NumEntries.PerOrigin.Q1";
constexpr char kNumEntriesQ3Histogram[] =
    "Storage.SharedStorage.Database.FileBacked.NumEntries.PerOrigin.Q3";
constexpr char kNumEntriesTotalHistogram[] =
    "Storage.SharedStorage.Database.FileBacked.NumEntries.Total";
constexpr char kNumOriginsHistogram[] =
    "Storage.SharedStorage.Database.FileBacked.NumOrigins";
constexpr char kIsFileBackedHistogram[] =
    "Storage.SharedStorage.Database.IsFileBacked";

}  // namespace

class SharedStorageDatabaseTest : public testing::Test {
 public:
  SharedStorageDatabaseTest() {
    special_storage_policy_ = base::MakeRefCounted<MockSpecialStoragePolicy>();
  }

  ~SharedStorageDatabaseTest() override = default;

  void SetUp() override {
    InitSharedStorageFeature();

    // Get a temporary directory for the test DB files.
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    file_name_ = temp_dir_.GetPath().AppendASCII("TestSharedStorage.db");
  }

  void TearDown() override {
    db_.reset();
    EXPECT_TRUE(temp_dir_.Delete());
  }

  // Initialize a shared storage database instance from the SQL file at
  // `relative_file_path` in the "storage/" subdirectory of test data.
  std::unique_ptr<SharedStorageDatabase> LoadFromFile(
      const char* relative_file_path) {
    if (!CreateDatabaseFromSQL(file_name_, relative_file_path)) {
      ADD_FAILURE() << "Failed loading " << relative_file_path;
      return nullptr;
    }

    return std::make_unique<SharedStorageDatabase>(
        file_name_, special_storage_policy_,
        SharedStorageOptions::Create()->GetDatabaseOptions());
  }

  sql::Database* SqlDB() { return db_ ? db_->db() : nullptr; }

  virtual void InitSharedStorageFeature() {
    scoped_feature_list_.InitAndEnableFeatureWithParameters(
        {blink::features::kSharedStorageAPI},
        {{"MaxSharedStorageInitTries", "1"},
         {"SharedStorageBitBudget", base::NumberToString(kBitBudget)},
         {"SharedStorageBudgetInterval",
          TimeDeltaToString(base::Hours(kBudgetIntervalHours))}});
  }

 protected:
  base::test::ScopedFeatureList scoped_feature_list_;
  base::test::SingleThreadTaskEnvironment task_environment_;
  base::ScopedTempDir temp_dir_;
  base::FilePath file_name_;
  scoped_refptr<storage::MockSpecialStoragePolicy> special_storage_policy_;
  std::unique_ptr<SharedStorageDatabase> db_;
  base::SimpleTestClock clock_;
  base::HistogramTester histogram_tester_;
};

// Test loading version 1 database.
TEST_F(SharedStorageDatabaseTest, Version1_LoadFromFile) {
  db_ = LoadFromFile("shared_storage.v1.sql");
  ASSERT_TRUE(db_);
  ASSERT_TRUE(db_->is_filebacked());

  // Override the clock and set to the last time in the file that is used to
  // make a budget withdrawal.
  db_->OverrideClockForTesting(&clock_);
  clock_.SetNow(base::Time::FromDeltaSinceWindowsEpoch(
      base::Microseconds(13269546593856733)));

  url::Origin google_com = url::Origin::Create(GURL("http://google.com/"));
  EXPECT_EQ(db_->Get(google_com, u"key1").data, u"value1");
  EXPECT_EQ(db_->Get(google_com, u"key2").data, u"value2");

  // Because the SQL database is lazy-initialized, wait to verify tables and
  // columns until after the first call to `Get()`.
  ASSERT_TRUE(SqlDB());
  VerifySharedStorageTablesAndColumns(*SqlDB());

  url::Origin youtube_com = url::Origin::Create(GURL("http://youtube.com/"));
  EXPECT_EQ(1L, db_->Length(youtube_com));

  url::Origin chromium_org = url::Origin::Create(GURL("http://chromium.org/"));
  EXPECT_EQ(db_->Get(chromium_org, u"a").data, u"");

  TestSharedStorageEntriesListener listener(
      task_environment_.GetMainThreadTaskRunner());
  EXPECT_EQ(OperationResult::kSuccess,
            db_->Keys(chromium_org, listener.BindNewPipeAndPassRemote()));
  listener.Flush();
  EXPECT_THAT(listener.TakeKeys(), ElementsAre(u"a", u"b", u"c"));
  EXPECT_EQ("", listener.error_message());
  EXPECT_EQ(1U, listener.BatchCount());
  listener.VerifyNoError();

  url::Origin google_org = url::Origin::Create(GURL("http://google.org/"));
  EXPECT_EQ(
      db_->Get(google_org, u"1").data,
      u"fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "fffffffffffffffff");
  EXPECT_EQ(db_->Get(google_org,
                     u"ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "ffffffffffffffffffffffffffffffffffffffffffffffffffffffff")
                .data,
            u"k");

  url::Origin abc_xyz = url::Origin::Create(GURL("http://abc.xyz"));
  url::Origin grow_with_google_com =
      url::Origin::Create(GURL("http://growwithgoogle.com"));
  url::Origin gv_com = url::Origin::Create(GURL("http://gv.com"));
  url::Origin waymo_com = url::Origin::Create(GURL("http://waymo.com"));
  url::Origin withgoogle_com =
      url::Origin::Create(GURL("http://withgoogle.com"));

  std::vector<url::Origin> origins;
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(abc_xyz, chromium_org, google_com,
                                   google_org, grow_with_google_com, gv_com,
                                   waymo_com, withgoogle_com, youtube_com));

  EXPECT_DOUBLE_EQ(kBitBudget - 5.3, db_->GetRemainingBudget(abc_xyz).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(chromium_org).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(google_com).bits);
  EXPECT_DOUBLE_EQ(kBitBudget - 4.0, db_->GetRemainingBudget(google_org).bits);
  EXPECT_DOUBLE_EQ(kBitBudget - 1.2,
                   db_->GetRemainingBudget(grow_with_google_com).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(gv_com).bits);
  EXPECT_DOUBLE_EQ(kBitBudget - 4.2, db_->GetRemainingBudget(waymo_com).bits);
  EXPECT_DOUBLE_EQ(kBitBudget - 1.0,
                   db_->GetRemainingBudget(withgoogle_com).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(youtube_com).bits);

  EXPECT_EQ(13266954476192362, db_->GetCreationTime(google_com)
                                   .time.ToDeltaSinceWindowsEpoch()
                                   .InMicroseconds());
  EXPECT_EQ(13266954593856733, db_->GetCreationTime(youtube_com)
                                   .time.ToDeltaSinceWindowsEpoch()
                                   .InMicroseconds());

  // Creation time for origin not present in the database will return
  // `OperationResult::kNotFound`.
  TimeResult result =
      db_->GetCreationTime(url::Origin::Create(GURL("http://a.test")));
  EXPECT_EQ(OperationResult::kNotFound, result.result);
  EXPECT_EQ(base::Time(), result.time);

  histogram_tester_.ExpectUniqueSample(kIsFileBackedHistogram, true, 1);
  histogram_tester_.ExpectUniqueSample(kFileSizeKBHistogram, 40, 1);
  histogram_tester_.ExpectUniqueSample(kNumOriginsHistogram, 9, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesTotalHistogram, 18, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMinHistogram, 1, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ1Histogram, 1, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMedianHistogram, 2, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ3Histogram, 3, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMaxHistogram, 4, 1);

  EXPECT_TRUE(db_->Destroy());
}

// Test loading version 1 database with no budget tables.
TEST_F(SharedStorageDatabaseTest, Version1_LoadFromFileNoBudgetTables) {
  db_ = LoadFromFile("shared_storage.v1.no_budget_table.sql");
  ASSERT_TRUE(db_);
  ASSERT_TRUE(db_->is_filebacked());

  url::Origin google_com = url::Origin::Create(GURL("http://google.com/"));
  EXPECT_EQ(db_->Get(google_com, u"key1").data, u"value1");
  EXPECT_EQ(db_->Get(google_com, u"key2").data, u"value2");

  // Because the SQL database is lazy-initialized, wait to verify tables and
  // columns until after the first call to `Get()`.
  ASSERT_TRUE(SqlDB());
  VerifySharedStorageTablesAndColumns(*SqlDB());

  url::Origin youtube_com = url::Origin::Create(GURL("http://youtube.com/"));
  EXPECT_EQ(1L, db_->Length(youtube_com));

  url::Origin chromium_org = url::Origin::Create(GURL("http://chromium.org/"));
  EXPECT_EQ(db_->Get(chromium_org, u"a").data, u"");

  TestSharedStorageEntriesListener listener(
      task_environment_.GetMainThreadTaskRunner());
  EXPECT_EQ(OperationResult::kSuccess,
            db_->Keys(chromium_org, listener.BindNewPipeAndPassRemote()));
  listener.Flush();
  EXPECT_THAT(listener.TakeKeys(), ElementsAre(u"a", u"b", u"c"));
  EXPECT_EQ("", listener.error_message());
  EXPECT_EQ(1U, listener.BatchCount());
  listener.VerifyNoError();

  url::Origin google_org = url::Origin::Create(GURL("http://google.org/"));
  EXPECT_EQ(
      db_->Get(google_org, u"1").data,
      u"fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "fffffffffffffffff");
  EXPECT_EQ(db_->Get(google_org,
                     u"ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "ffffffffffffffffffffffffffffffffffffffffffffffffffffffff")
                .data,
            u"k");

  url::Origin abc_xyz = url::Origin::Create(GURL("http://abc.xyz"));
  url::Origin grow_with_google_com =
      url::Origin::Create(GURL("http://growwithgoogle.com"));
  url::Origin gv_com = url::Origin::Create(GURL("http://gv.com"));
  url::Origin waymo_com = url::Origin::Create(GURL("http://waymo.com"));
  url::Origin withgoogle_com =
      url::Origin::Create(GURL("http://withgoogle.com"));

  std::vector<url::Origin> origins;
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(abc_xyz, chromium_org, google_com,
                                   google_org, grow_with_google_com, gv_com,
                                   waymo_com, withgoogle_com, youtube_com));

  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(abc_xyz).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(chromium_org).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(google_com).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(google_org).bits);
  EXPECT_DOUBLE_EQ(kBitBudget,
                   db_->GetRemainingBudget(grow_with_google_com).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(gv_com).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(waymo_com).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(withgoogle_com).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(youtube_com).bits);

  histogram_tester_.ExpectUniqueSample(kIsFileBackedHistogram, true, 1);
  histogram_tester_.ExpectUniqueSample(kFileSizeKBHistogram, 40, 1);
  histogram_tester_.ExpectUniqueSample(kNumOriginsHistogram, 9, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesTotalHistogram, 18, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMinHistogram, 1, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ1Histogram, 1, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMedianHistogram, 2, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ3Histogram, 3, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMaxHistogram, 4, 1);

  EXPECT_TRUE(db_->Destroy());
}

TEST_F(SharedStorageDatabaseTest, Version1_DestroyTooNew) {
  // Initialization should fail, since the last compatible version number
  // is too high.
  db_ = LoadFromFile("shared_storage.v1.init_too_new.sql");
  ASSERT_TRUE(db_);
  ASSERT_TRUE(db_->is_filebacked());
  ASSERT_TRUE(SqlDB());

  // Call an operation so that the database will attempt to be lazy-initialized.
  const url::Origin kOrigin = url::Origin::Create(GURL("http://www.a.com"));
  EXPECT_EQ(OperationResult::kInitFailure, db_->Set(kOrigin, u"key", u"value"));
  ASSERT_FALSE(db_->IsOpenForTesting());
  EXPECT_EQ(InitStatus::kTooNew, db_->DBStatusForTesting());

  // Test that other operations likewise fail, in order to exercise these code
  // paths.
  EXPECT_EQ(OperationResult::kInitFailure, db_->Get(kOrigin, u"key").result);
  EXPECT_EQ(OperationResult::kInitFailure,
            db_->Append(kOrigin, u"key", u"value"));
  EXPECT_EQ(OperationResult::kInitFailure, db_->Delete(kOrigin, u"key"));
  EXPECT_EQ(OperationResult::kInitFailure, db_->Clear(kOrigin));
  EXPECT_EQ(-1, db_->Length(kOrigin));
  EXPECT_EQ(OperationResult::kInitFailure,
            db_->PurgeMatchingOrigins(StorageKeyPolicyMatcherFunction(),
                                      base::Time::Min(), base::Time::Max(),
                                      /*perform_storage_cleanup=*/false));
  EXPECT_EQ(OperationResult::kInitFailure, db_->PurgeStaleOrigins());
  EXPECT_EQ(OperationResult::kInitFailure,
            db_->GetEntriesForDevTools(kOrigin).result);

  auto metadata = db_->GetMetadata(kOrigin);
  EXPECT_EQ(OperationResult::kInitFailure, metadata.time_result);
  EXPECT_EQ(OperationResult::kInitFailure, metadata.budget_result);

  // Test that it is still OK to Destroy() the database.
  EXPECT_TRUE(db_->Destroy());
}

TEST_F(SharedStorageDatabaseTest, Version0_DestroyTooOld) {
  // Initialization should fail, since the current version number
  // is too low and we're forcing there not to be a retry attempt.
  db_ = LoadFromFile("shared_storage.v0.init_too_old.sql");
  ASSERT_TRUE(db_);
  ASSERT_TRUE(db_->is_filebacked());
  ASSERT_TRUE(SqlDB());

  // Call an operation so that the database will attempt to be lazy-initialized.
  EXPECT_EQ(OperationResult::kInitFailure,
            db_->Set(url::Origin::Create(GURL("http://www.a.com")), u"key",
                     u"value"));
  ASSERT_FALSE(db_->IsOpenForTesting());
  EXPECT_EQ(InitStatus::kTooOld, db_->DBStatusForTesting());

  // Test that it is still OK to Destroy() the database.
  EXPECT_TRUE(db_->Destroy());
}

class SharedStorageDatabaseParamTest
    : public SharedStorageDatabaseTest,
      public testing::WithParamInterface<SharedStorageWrappedBool> {
 public:
  void SetUp() override {
    SharedStorageDatabaseTest::SetUp();

    auto options = SharedStorageOptions::Create()->GetDatabaseOptions();
    base::FilePath db_path =
        (GetParam().in_memory_only) ? base::FilePath() : file_name_;
    db_ = std::make_unique<SharedStorageDatabase>(
        db_path, special_storage_policy_, std::move(options));
    db_->OverrideClockForTesting(&clock_);

    ASSERT_EQ(GetParam().in_memory_only, !db_->is_filebacked());
  }

  void TearDown() override {
    CheckInitHistograms();
    SharedStorageDatabaseTest::TearDown();
  }

  void InitSharedStorageFeature() override {
    scoped_feature_list_.InitAndEnableFeatureWithParameters(
        {blink::features::kSharedStorageAPI},
        {{"MaxSharedStorageEntriesPerOrigin",
          base::NumberToString(kMaxEntriesPerOrigin)},
         {"MaxSharedStorageStringLength",
          base::NumberToString(kMaxStringLength)},
         {"SharedStorageBitBudget", base::NumberToString(kBitBudget)},
         {"SharedStorageBudgetInterval",
          TimeDeltaToString(base::Hours(kBudgetIntervalHours))},
         {"SharedStorageOriginStalenessThreshold",
          TimeDeltaToString(base::Days(kOriginStalenessThresholdDays))}});
  }

  void CheckInitHistograms() {
    histogram_tester_.ExpectUniqueSample(kIsFileBackedHistogram,
                                         db_->is_filebacked(), 1);
    if (db_->is_filebacked()) {
      histogram_tester_.ExpectUniqueSample(kFileSizeKBHistogram, 29, 1);
      histogram_tester_.ExpectUniqueSample(kNumOriginsHistogram, 0, 1);
      histogram_tester_.ExpectUniqueSample(kNumEntriesTotalHistogram, 0, 1);
    }
  }
};

INSTANTIATE_TEST_SUITE_P(All,
                         SharedStorageDatabaseParamTest,
                         testing::ValuesIn(GetSharedStorageWrappedBools()),
                         testing::PrintToStringParamName());

TEST_P(SharedStorageDatabaseParamTest, BasicOperations) {
  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(db_->Get(kOrigin1, u"key1").data, u"value1");

  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value2"));
  EXPECT_EQ(db_->Get(kOrigin1, u"key1").data, u"value2");

  EXPECT_EQ(OperationResult::kSuccess, db_->Delete(kOrigin1, u"key1"));
  EXPECT_EQ(OperationResult::kNotFound, db_->Get(kOrigin1, u"key1").result);

  // Check that trying to retrieve the empty key returns
  // `OperationResult::kNotFound` rather than `OperationResult::kSqlError`,
  // even though the input is considered invalid.
  GetResult result = db_->Get(kOrigin1, u"");
  EXPECT_EQ(OperationResult::kNotFound, result.result);
  EXPECT_TRUE(result.data.empty());

  // Check that trying to delete the empty key doesn't give an error, even
  // though the input is invalid and no value is found to delete.
  EXPECT_EQ(OperationResult::kSuccess, db_->Delete(kOrigin1, u""));
}

TEST_P(SharedStorageDatabaseParamTest, IgnoreIfPresent) {
  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(db_->Get(kOrigin1, u"key1").data, u"value1");

  // The database does not set a new value for "key1", but retains the
  // previously set value "value1" because `behavior` is `kIgnoreIfPresent`.
  EXPECT_EQ(OperationResult::kIgnored,
            db_->Set(kOrigin1, u"key1", u"value2",
                     /*behavior=*/SetBehavior::kIgnoreIfPresent));
  EXPECT_EQ(db_->Get(kOrigin1, u"key1").data, u"value1");

  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key2", u"value1"));
  EXPECT_EQ(db_->Get(kOrigin1, u"key2").data, u"value1");

  // Having `behavior` set to `kDefault` makes `Set()` override any previous
  // value.
  EXPECT_EQ(OperationResult::kSet,
            db_->Set(kOrigin1, u"key2", u"value2",
                     /*behavior=*/SetBehavior::kDefault));
  EXPECT_EQ(db_->Get(kOrigin1, u"key2").data, u"value2");

  const url::Origin kOrigin2 =
      url::Origin::Create(GURL("http://www.example2.test"));

  // If no previous value exists, it makes no difference whether
  // `behavior` is set to `kDefault` or `kIgnoreIfPresent`.
  EXPECT_EQ(OperationResult::kSet,
            db_->Set(kOrigin2, u"key1", u"value1",
                     /*behavior=*/SetBehavior::kIgnoreIfPresent));
  EXPECT_EQ(db_->Get(kOrigin2, u"key1").data, u"value1");

  EXPECT_EQ(OperationResult::kSet,
            db_->Set(kOrigin2, u"key2", u"value2",
                     /*behavior=*/SetBehavior::kDefault));
  EXPECT_EQ(db_->Get(kOrigin2, u"key2").data, u"value2");
}

TEST_P(SharedStorageDatabaseParamTest, Append) {
  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Append(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(db_->Get(kOrigin1, u"key1").data, u"value1");

  EXPECT_EQ(OperationResult::kSet, db_->Append(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(db_->Get(kOrigin1, u"key1").data, u"value1value1");

  EXPECT_EQ(OperationResult::kSet, db_->Append(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(db_->Get(kOrigin1, u"key1").data, u"value1value1value1");
}

TEST_P(SharedStorageDatabaseParamTest, Length) {
  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  EXPECT_EQ(0L, db_->Length(kOrigin1));

  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(1L, db_->Length(kOrigin1));

  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key2", u"value2"));
  EXPECT_EQ(2L, db_->Length(kOrigin1));

  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key2", u"value3"));
  EXPECT_EQ(2L, db_->Length(kOrigin1));

  const url::Origin kOrigin2 =
      url::Origin::Create(GURL("http://www.example2.test"));
  EXPECT_EQ(0L, db_->Length(kOrigin2));

  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key1", u"value1"));
  EXPECT_EQ(1L, db_->Length(kOrigin2));
  EXPECT_EQ(2L, db_->Length(kOrigin1));

  EXPECT_EQ(OperationResult::kSuccess, db_->Delete(kOrigin2, u"key1"));
  EXPECT_EQ(0L, db_->Length(kOrigin2));
  EXPECT_EQ(2L, db_->Length(kOrigin1));

  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key3", u"value3"));
  EXPECT_EQ(3L, db_->Length(kOrigin1));
  EXPECT_EQ(0L, db_->Length(kOrigin2));
}

TEST_P(SharedStorageDatabaseParamTest, Keys) {
  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  TestSharedStorageEntriesListenerUtility utility(
      task_environment_.GetMainThreadTaskRunner());
  size_t id1 = utility.RegisterListener();
  EXPECT_EQ(OperationResult::kSuccess,
            db_->Keys(kOrigin1, utility.BindNewPipeAndPassRemoteForId(id1)));
  utility.FlushForId(id1);
  EXPECT_TRUE(utility.TakeKeysForId(id1).empty());
  EXPECT_EQ(1U, utility.BatchCountForId(id1));
  utility.VerifyNoErrorForId(id1);

  EXPECT_EQ(InitStatus::kUnattempted, db_->DBStatusForTesting());

  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key2", u"value2"));

  size_t id2 = utility.RegisterListener();
  EXPECT_EQ(OperationResult::kSuccess,
            db_->Keys(kOrigin1, utility.BindNewPipeAndPassRemoteForId(id2)));
  utility.FlushForId(id2);
  EXPECT_THAT(utility.TakeKeysForId(id2), ElementsAre(u"key1", u"key2"));
  EXPECT_EQ(1U, utility.BatchCountForId(id2));
  utility.VerifyNoErrorForId(id2);

  const url::Origin kOrigin2 =
      url::Origin::Create(GURL("http://www.example2.test"));
  size_t id3 = utility.RegisterListener();
  EXPECT_EQ(OperationResult::kSuccess,
            db_->Keys(kOrigin2, utility.BindNewPipeAndPassRemoteForId(id3)));
  utility.FlushForId(id3);
  EXPECT_TRUE(utility.TakeKeysForId(id3).empty());
  EXPECT_EQ(1U, utility.BatchCountForId(id3));
  utility.VerifyNoErrorForId(id3);

  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key3", u"value3"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key2", u"value2"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key1", u"value1"));

  size_t id4 = utility.RegisterListener();
  EXPECT_EQ(OperationResult::kSuccess,
            db_->Keys(kOrigin2, utility.BindNewPipeAndPassRemoteForId(id4)));
  utility.FlushForId(id4);
  EXPECT_THAT(utility.TakeKeysForId(id4),
              ElementsAre(u"key1", u"key2", u"key3"));
  EXPECT_EQ(1U, utility.BatchCountForId(id4));
  utility.VerifyNoErrorForId(id4);

  EXPECT_EQ(OperationResult::kSuccess, db_->Delete(kOrigin2, u"key2"));

  size_t id5 = utility.RegisterListener();
  EXPECT_EQ(OperationResult::kSuccess,
            db_->Keys(kOrigin2, utility.BindNewPipeAndPassRemoteForId(id5)));
  utility.FlushForId(id5);
  EXPECT_THAT(utility.TakeKeysForId(id5), ElementsAre(u"key1", u"key3"));
  EXPECT_EQ(1U, utility.BatchCountForId(id5));
  utility.VerifyNoErrorForId(id5);
}

TEST_P(SharedStorageDatabaseParamTest, Entries) {
  url::Origin kOrigin1 = url::Origin::Create(GURL("http://www.example1.test"));
  TestSharedStorageEntriesListenerUtility utility(
      task_environment_.GetMainThreadTaskRunner());
  size_t id1 = utility.RegisterListener();
  EXPECT_EQ(OperationResult::kSuccess,
            db_->Entries(kOrigin1, utility.BindNewPipeAndPassRemoteForId(id1)));
  utility.FlushForId(id1);
  EXPECT_TRUE(utility.TakeEntriesForId(id1).empty());
  EXPECT_EQ(1U, utility.BatchCountForId(id1));
  utility.VerifyNoErrorForId(id1);

  EXPECT_EQ(InitStatus::kUnattempted, db_->DBStatusForTesting());

  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key2", u"value2"));

  size_t id2 = utility.RegisterListener();
  EXPECT_EQ(OperationResult::kSuccess,
            db_->Entries(kOrigin1, utility.BindNewPipeAndPassRemoteForId(id2)));
  utility.FlushForId(id2);
  EXPECT_THAT(utility.TakeEntriesForId(id2),
              ElementsAre(std::make_pair(u"key1", u"value1"),
                          std::make_pair(u"key2", u"value2")));
  EXPECT_EQ(1U, utility.BatchCountForId(id2));
  utility.VerifyNoErrorForId(id2);

  url::Origin kOrigin2 = url::Origin::Create(GURL("http://www.example2.test"));
  size_t id3 = utility.RegisterListener();
  EXPECT_EQ(OperationResult::kSuccess,
            db_->Entries(kOrigin2, utility.BindNewPipeAndPassRemoteForId(id3)));
  utility.FlushForId(id3);
  EXPECT_TRUE(utility.TakeEntriesForId(id3).empty());
  EXPECT_EQ(1U, utility.BatchCountForId(id3));
  utility.VerifyNoErrorForId(id3);

  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key3", u"value3"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key2", u"value2"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key1", u"value1"));

  size_t id4 = utility.RegisterListener();
  EXPECT_EQ(OperationResult::kSuccess,
            db_->Entries(kOrigin2, utility.BindNewPipeAndPassRemoteForId(id4)));
  utility.FlushForId(id4);
  EXPECT_THAT(utility.TakeEntriesForId(id4),
              ElementsAre(std::make_pair(u"key1", u"value1"),
                          std::make_pair(u"key2", u"value2"),
                          std::make_pair(u"key3", u"value3")));
  EXPECT_EQ(1U, utility.BatchCountForId(id4));
  utility.VerifyNoErrorForId(id4);

  EXPECT_EQ(OperationResult::kSuccess, db_->Delete(kOrigin2, u"key2"));

  size_t id5 = utility.RegisterListener();
  EXPECT_EQ(OperationResult::kSuccess,
            db_->Entries(kOrigin2, utility.BindNewPipeAndPassRemoteForId(id5)));
  utility.FlushForId(id5);
  EXPECT_THAT(utility.TakeEntriesForId(id5),
              ElementsAre(std::make_pair(u"key1", u"value1"),
                          std::make_pair(u"key3", u"value3")));
  EXPECT_EQ(1U, utility.BatchCountForId(id5));
  utility.VerifyNoErrorForId(id5);
}

TEST_P(SharedStorageDatabaseParamTest, Clear) {
  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key2", u"value2"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key3", u"value3"));
  EXPECT_EQ(3L, db_->Length(kOrigin1));

  const url::Origin kOrigin2 =
      url::Origin::Create(GURL("http://www.example2.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key2", u"value2"));
  EXPECT_EQ(2L, db_->Length(kOrigin2));

  EXPECT_EQ(OperationResult::kSuccess, db_->Clear(kOrigin1));
  EXPECT_EQ(0L, db_->Length(kOrigin1));
  EXPECT_EQ(2L, db_->Length(kOrigin2));

  EXPECT_EQ(OperationResult::kSuccess, db_->Clear(kOrigin2));
  EXPECT_EQ(0L, db_->Length(kOrigin2));
}

TEST_P(SharedStorageDatabaseParamTest, FetchOrigins) {
  EXPECT_TRUE(db_->FetchOrigins().empty());

  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key2", u"value2"));
  EXPECT_EQ(2L, db_->Length(kOrigin1));

  const url::Origin kOrigin2 =
      url::Origin::Create(GURL("http://www.example2.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key1", u"value1"));
  EXPECT_EQ(1L, db_->Length(kOrigin2));

  const url::Origin kOrigin3 =
      url::Origin::Create(GURL("http://www.example3.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin3, u"key1", u"value1"));
  EXPECT_EQ(1L, db_->Length(kOrigin3));

  const url::Origin kOrigin4 =
      url::Origin::Create(GURL("http://www.example4.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin4, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin4, u"key2", u"value2"));
  EXPECT_EQ(2L, db_->Length(kOrigin4));

  std::vector<url::Origin> origins;
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(kOrigin1, kOrigin2, kOrigin3, kOrigin4));

  EXPECT_EQ(OperationResult::kSuccess, db_->Clear(kOrigin1));
  EXPECT_EQ(0L, db_->Length(kOrigin1));

  EXPECT_EQ(OperationResult::kSuccess, db_->Delete(kOrigin2, u"key1"));
  EXPECT_EQ(0L, db_->Length(kOrigin2));

  origins.clear();
  EXPECT_TRUE(origins.empty());
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(kOrigin3, kOrigin4));

  origins.clear();
  EXPECT_TRUE(origins.empty());
  for (const auto& info : db_->FetchOrigins(/*exclude_empty_origins=*/false))
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(kOrigin1, kOrigin2, kOrigin3, kOrigin4));
}

TEST_P(SharedStorageDatabaseParamTest, MakeBudgetWithdrawal) {
  clock_.SetNow(base::Time::Now());

  // There should be no entries in the budget table.
  EXPECT_EQ(0L, db_->GetTotalNumBudgetEntriesForTesting());

  // SQL database hasn't yet been lazy-initialized. Nevertheless, remaining
  // budgets should be returned as the max possible.
  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(kOrigin1).bits);
  const url::Origin kOrigin2 =
      url::Origin::Create(GURL("http://www.example2.test"));
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(kOrigin2).bits);

  // A withdrawal for `kOrigin1` doesn't affect `kOrigin2`.
  EXPECT_EQ(OperationResult::kSuccess,
            db_->MakeBudgetWithdrawal(kOrigin1, 1.75));
  EXPECT_DOUBLE_EQ(kBitBudget - 1.75, db_->GetRemainingBudget(kOrigin1).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(kOrigin2).bits);
  EXPECT_EQ(1L, db_->GetNumBudgetEntriesForTesting(kOrigin1));
  EXPECT_EQ(1L, db_->GetTotalNumBudgetEntriesForTesting());

  // An additional withdrawal for `kOrigin1` at or near the same time as the
  // previous one is debited appropriately.
  EXPECT_EQ(OperationResult::kSuccess,
            db_->MakeBudgetWithdrawal(kOrigin1, 2.5));
  EXPECT_DOUBLE_EQ(kBitBudget - 1.75 - 2.5,
                   db_->GetRemainingBudget(kOrigin1).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(kOrigin2).bits);
  EXPECT_EQ(2L, db_->GetNumBudgetEntriesForTesting(kOrigin1));
  EXPECT_EQ(2L, db_->GetTotalNumBudgetEntriesForTesting());

  // A withdrawal for `kOrigin2` doesn't affect `kOrigin1`.
  EXPECT_EQ(OperationResult::kSuccess,
            db_->MakeBudgetWithdrawal(kOrigin2, 3.4));
  EXPECT_DOUBLE_EQ(kBitBudget - 3.4, db_->GetRemainingBudget(kOrigin2).bits);
  EXPECT_DOUBLE_EQ(kBitBudget - 1.75 - 2.5,
                   db_->GetRemainingBudget(kOrigin1).bits);
  EXPECT_EQ(2L, db_->GetNumBudgetEntriesForTesting(kOrigin1));
  EXPECT_EQ(1L, db_->GetNumBudgetEntriesForTesting(kOrigin2));
  EXPECT_EQ(3L, db_->GetTotalNumBudgetEntriesForTesting());

  // Advance halfway through the lookback window.
  clock_.Advance(base::Hours(kBudgetIntervalHours) / 2);

  // Remaining budgets continue to take into account the withdrawals above, as
  // they are still within the lookback window.
  EXPECT_DOUBLE_EQ(kBitBudget - 3.4, db_->GetRemainingBudget(kOrigin2).bits);
  EXPECT_DOUBLE_EQ(kBitBudget - 1.75 - 2.5,
                   db_->GetRemainingBudget(kOrigin1).bits);

  // An additional withdrawal for `kOrigin1` at a later time from previous ones
  // is debited appropriately.
  EXPECT_EQ(OperationResult::kSuccess,
            db_->MakeBudgetWithdrawal(kOrigin1, 1.0));
  EXPECT_DOUBLE_EQ(kBitBudget - 1.75 - 2.5 - 1.0,
                   db_->GetRemainingBudget(kOrigin1).bits);
  EXPECT_DOUBLE_EQ(kBitBudget - 3.4, db_->GetRemainingBudget(kOrigin2).bits);
  EXPECT_EQ(3L, db_->GetNumBudgetEntriesForTesting(kOrigin1));
  EXPECT_EQ(1L, db_->GetNumBudgetEntriesForTesting(kOrigin2));
  EXPECT_EQ(4L, db_->GetTotalNumBudgetEntriesForTesting());

  // Advance to the end of the initial lookback window, plus an additional
  // microsecond to move past that window.
  clock_.Advance(base::Hours(kBudgetIntervalHours) / 2 + base::Microseconds(1));

  // Now only the single debit made within the current lookback window is
  // counted, although the entries are still in the table because we haven't
  // called `PurgeStaleWithdrawals()`.
  EXPECT_DOUBLE_EQ(kBitBudget - 1.0, db_->GetRemainingBudget(kOrigin1).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(kOrigin2).bits);
  EXPECT_EQ(3L, db_->GetNumBudgetEntriesForTesting(kOrigin1));
  EXPECT_EQ(1L, db_->GetNumBudgetEntriesForTesting(kOrigin2));
  EXPECT_EQ(4L, db_->GetTotalNumBudgetEntriesForTesting());

  // After `PurgeStaleOrigins()` runs, there will only be the most recent
  // debit left in the budget table.
  EXPECT_EQ(OperationResult::kSuccess, db_->PurgeStaleOrigins());
  EXPECT_DOUBLE_EQ(kBitBudget - 1.0, db_->GetRemainingBudget(kOrigin1).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(kOrigin2).bits);
  EXPECT_EQ(1L, db_->GetNumBudgetEntriesForTesting(kOrigin1));
  EXPECT_EQ(0L, db_->GetNumBudgetEntriesForTesting(kOrigin2));
  EXPECT_EQ(1L, db_->GetTotalNumBudgetEntriesForTesting());

  // Advance to where the last debit should no longer be in the lookback window.
  clock_.Advance(base::Hours(kBudgetIntervalHours) / 2);

  // Remaining budgets should be back at the max, although there is still an
  // entry in the table.
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(kOrigin1).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(kOrigin2).bits);
  EXPECT_EQ(1L, db_->GetNumBudgetEntriesForTesting(kOrigin1));
  EXPECT_EQ(1L, db_->GetTotalNumBudgetEntriesForTesting());

  // After `PurgeStaleOrigins()` runs, the budget table will be empty.
  EXPECT_EQ(OperationResult::kSuccess, db_->PurgeStaleOrigins());
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(kOrigin1).bits);
  EXPECT_DOUBLE_EQ(kBitBudget, db_->GetRemainingBudget(kOrigin2).bits);
  EXPECT_EQ(0L, db_->GetTotalNumBudgetEntriesForTesting());
}

TEST_P(SharedStorageDatabaseParamTest,
       DeleteAllEntriesBeforeExpiration_CreationTimeUnchanged) {
  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key2", u"value2"));
  EXPECT_EQ(2L, db_->Length(kOrigin1));
  base::Time creation_time1 = db_->GetCreationTime(kOrigin1).time;

  const url::Origin kOrigin2 =
      url::Origin::Create(GURL("http://www.example2.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key3", u"value3"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key2", u"value2"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key1", u"value1"));
  EXPECT_EQ(3L, db_->Length(kOrigin2));
  base::Time creation_time2 = db_->GetCreationTime(kOrigin2).time;

  // Creation time does not change when all of `kOrigin1`'s entries are deleted
  // via `Delete()` before expiration.
  EXPECT_EQ(OperationResult::kSuccess, db_->Delete(kOrigin1, u"key1"));
  EXPECT_EQ(OperationResult::kSuccess, db_->Delete(kOrigin1, u"key2"));
  EXPECT_EQ(0L, db_->Length(kOrigin1));
  EXPECT_EQ(creation_time1, db_->GetCreationTime(kOrigin1).time);

  // Creation time does not change when all of `kOrigin2`'s entries are deleted
  // via `Clear()` before expiration.
  EXPECT_EQ(OperationResult::kSuccess, db_->Clear(kOrigin2));
  EXPECT_EQ(0L, db_->Length(kOrigin2));
  EXPECT_EQ(creation_time2, db_->GetCreationTime(kOrigin2).time);

  // Creation time does not change when `kOrigin1` inserts a new entry before
  // expiration.
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(1L, db_->Length(kOrigin1));
  EXPECT_EQ(creation_time1, db_->GetCreationTime(kOrigin1).time);
}

TEST_P(SharedStorageDatabaseParamTest,
       DeleteAllEntriesAfterExpiration_CreationTimeUnchanged) {
  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key2", u"value2"));
  EXPECT_EQ(2L, db_->Length(kOrigin1));
  base::Time creation_time1 = db_->GetCreationTime(kOrigin1).time;

  const url::Origin kOrigin2 =
      url::Origin::Create(GURL("http://www.example2.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key3", u"value3"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key2", u"value2"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key1", u"value1"));
  EXPECT_EQ(3L, db_->Length(kOrigin2));
  base::Time creation_time2 = db_->GetCreationTime(kOrigin2).time;

  clock_.Advance(base::Days(kOriginStalenessThresholdDays) +
                 base::Microseconds(1));

  // Creation time will remain the same when all of `kOrigin1`'s entries are
  // deleted via `Delete()` after expiration but `PurgeStaleOrigins()` has not
  // yet been called.
  EXPECT_EQ(OperationResult::kSuccess, db_->Delete(kOrigin1, u"key1"));
  EXPECT_EQ(OperationResult::kSuccess, db_->Delete(kOrigin1, u"key2"));
  EXPECT_EQ(0L, db_->Length(kOrigin1));
  EXPECT_EQ(creation_time1, db_->GetCreationTime(kOrigin1).time);

  // Creation time will remain the same when all of `kOrigin2`'s entries are
  // deleted via `Clear()` after expiration but `PurgeStaleOrigins()` has not
  // yet been called.
  EXPECT_EQ(OperationResult::kSuccess, db_->Clear(kOrigin2));
  EXPECT_EQ(0L, db_->Length(kOrigin2));
  EXPECT_EQ(creation_time2, db_->GetCreationTime(kOrigin2).time);

  EXPECT_EQ(OperationResult::kSuccess, db_->PurgeStaleOrigins());

  // Creation times should not be found after a purge of stale origins.
  EXPECT_EQ(OperationResult::kNotFound, db_->GetCreationTime(kOrigin1).result);
  EXPECT_EQ(OperationResult::kNotFound, db_->GetCreationTime(kOrigin2).result);

  // Creation time is updated when `kOrigin1` inserts a new entry after previous
  // expiration and purge.
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(1L, db_->Length(kOrigin1));
  EXPECT_LT(creation_time1, db_->GetCreationTime(kOrigin1).time);

  // Creation time is updated when `kOrigin2` inserts a new entry after previous
  // expiration and purge.
  EXPECT_EQ(OperationResult::kSet, db_->Append(kOrigin2, u"key1", u"value1"));
  EXPECT_EQ(1L, db_->Length(kOrigin2));
  EXPECT_LT(creation_time2, db_->GetCreationTime(kOrigin2).time);
}

TEST_P(SharedStorageDatabaseParamTest,
       ClearNonexistentOrigin_NotAddedToPerOriginMapping) {
  // Initialize the database.
  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));

  const url::Origin kOrigin2 =
      url::Origin::Create(GURL("http://www.example2.test"));

  // Clear an origin not existing in the database. Its creation time will not be
  // found.
  EXPECT_EQ(OperationResult::kSuccess, db_->Clear(kOrigin2));
  EXPECT_EQ(OperationResult::kNotFound, db_->GetCreationTime(kOrigin2).result);
}

class SharedStorageDatabasePurgeMatchingOriginsParamTest
    : public SharedStorageDatabaseTest,
      public testing::WithParamInterface<PurgeMatchingOriginsParams> {
 public:
  void SetUp() override {
    SharedStorageDatabaseTest::SetUp();

    auto options = SharedStorageOptions::Create()->GetDatabaseOptions();
    base::FilePath db_path =
        (GetParam().in_memory_only) ? base::FilePath() : file_name_;
    db_ = std::make_unique<SharedStorageDatabase>(
        db_path, special_storage_policy_, std::move(options));
    db_->OverrideClockForTesting(&clock_);
  }

  void InitSharedStorageFeature() override {
    scoped_feature_list_.InitAndEnableFeatureWithParameters(
        {blink::features::kSharedStorageAPI},
        {{"MaxSharedStorageEntriesPerOrigin",
          base::NumberToString(kMaxEntriesPerOrigin)},
         {"MaxSharedStorageStringLength",
          base::NumberToString(kMaxStringLength)}});
  }
};

INSTANTIATE_TEST_SUITE_P(All,
                         SharedStorageDatabasePurgeMatchingOriginsParamTest,
                         testing::ValuesIn(GetPurgeMatchingOriginsParams()),
                         testing::PrintToStringParamName());

TEST_P(SharedStorageDatabasePurgeMatchingOriginsParamTest, AllTime) {
  EXPECT_TRUE(db_->FetchOrigins().empty());

  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key2", u"value2"));
  EXPECT_EQ(2L, db_->Length(kOrigin1));

  const url::Origin kOrigin2 =
      url::Origin::Create(GURL("http://www.example2.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key1", u"value1"));
  EXPECT_EQ(1L, db_->Length(kOrigin2));

  const url::Origin kOrigin3 =
      url::Origin::Create(GURL("http://www.example3.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin3, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin3, u"key2", u"value2"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin3, u"key3", u"value3"));
  EXPECT_EQ(3L, db_->Length(kOrigin3));

  std::vector<url::Origin> origins;
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(kOrigin1, kOrigin2, kOrigin3));

  EXPECT_EQ(
      OperationResult::kSuccess,
      db_->PurgeMatchingOrigins(
          StorageKeyPolicyMatcherFunctionUtility::MakeMatcherFunction(
              {kOrigin1}),
          base::Time(), base::Time::Max(), GetParam().perform_storage_cleanup));

  // `kOrigin1` is cleared. The other origins are not.
  EXPECT_EQ(0L, db_->Length(kOrigin1));
  EXPECT_EQ(1L, db_->Length(kOrigin2));
  EXPECT_EQ(3L, db_->Length(kOrigin3));

  origins.clear();
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(kOrigin2, kOrigin3));

  EXPECT_EQ(
      OperationResult::kSuccess,
      db_->PurgeMatchingOrigins(
          StorageKeyPolicyMatcherFunctionUtility::MakeMatcherFunction(
              {kOrigin2, kOrigin3}),
          base::Time(), base::Time::Max(), GetParam().perform_storage_cleanup));

  // All three origins should be cleared.
  EXPECT_EQ(0L, db_->Length(kOrigin1));
  EXPECT_EQ(0L, db_->Length(kOrigin2));
  EXPECT_EQ(0L, db_->Length(kOrigin3));

  EXPECT_TRUE(db_->FetchOrigins().empty());

  // There is no error from trying to clear an origin that isn't in the
  // database.
  EXPECT_EQ(
      OperationResult::kSuccess,
      db_->PurgeMatchingOrigins(
          StorageKeyPolicyMatcherFunctionUtility::MakeMatcherFunction(
              {"http://www.example4.test"}),
          base::Time(), base::Time::Max(), GetParam().perform_storage_cleanup));
}

TEST_P(SharedStorageDatabasePurgeMatchingOriginsParamTest, SinceThreshold) {
  EXPECT_TRUE(db_->FetchOrigins().empty());

  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key2", u"value2"));
  EXPECT_EQ(2L, db_->Length(kOrigin1));

  clock_.SetNow(base::Time::Now());
  clock_.Advance(base::Milliseconds(50));

  // Time threshold that will be used as a starting point for deletion.
  base::Time threshold1 = clock_.Now();

  const url::Origin kOrigin2 =
      url::Origin::Create(GURL("http://www.example2.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key1", u"value1"));
  EXPECT_EQ(1L, db_->Length(kOrigin2));

  const url::Origin kOrigin3 =
      url::Origin::Create(GURL("http://www.example3.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin3, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin3, u"key2", u"value2"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin3, u"key3", u"value3"));
  EXPECT_EQ(3L, db_->Length(kOrigin3));

  const url::Origin kOrigin4 =
      url::Origin::Create(GURL("http://www.example4.test"));

  clock_.SetNow(base::Time::Now());
  clock_.Advance(base::Milliseconds(50));

  // Time threshold that will be used as a starting point for deletion.
  base::Time threshold2 = clock_.Now();

  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin4, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin4, u"key2", u"value2"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin4, u"key3", u"value3"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin4, u"key4", u"value4"));
  EXPECT_EQ(4L, db_->Length(kOrigin4));

  std::vector<url::Origin> origins;
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(kOrigin1, kOrigin2, kOrigin3, kOrigin4));

  // Read from `kOrigin1`.
  EXPECT_EQ(db_->Get(kOrigin1, u"key1").data, u"value1");

  EXPECT_EQ(
      OperationResult::kSuccess,
      db_->PurgeMatchingOrigins(
          StorageKeyPolicyMatcherFunctionUtility::MakeMatcherFunction(
              {kOrigin2, kOrigin4}),
          threshold2, base::Time::Max(), GetParam().perform_storage_cleanup));

  // `kOrigin4` is cleared. The other origins are not.
  EXPECT_EQ(2L, db_->Length(kOrigin1));
  EXPECT_EQ(1L, db_->Length(kOrigin2));
  EXPECT_EQ(3L, db_->Length(kOrigin3));
  EXPECT_EQ(0L, db_->Length(kOrigin4));

  origins.clear();
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(kOrigin1, kOrigin2, kOrigin3));

  EXPECT_EQ(
      OperationResult::kSuccess,
      db_->PurgeMatchingOrigins(
          StorageKeyPolicyMatcherFunctionUtility::MakeMatcherFunction(
              {kOrigin1, kOrigin3, kOrigin4}),
          threshold1, base::Time::Max(), GetParam().perform_storage_cleanup));

  // `kOrigin3` is cleared. The others weren't modified within the given time
  // period.
  EXPECT_EQ(2L, db_->Length(kOrigin1));
  EXPECT_EQ(1L, db_->Length(kOrigin2));
  EXPECT_EQ(0L, db_->Length(kOrigin3));
  EXPECT_EQ(0L, db_->Length(kOrigin4));

  origins.clear();
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(kOrigin1, kOrigin2));

  // There is no error from trying to clear an origin that isn't in the
  // database.
  EXPECT_EQ(
      OperationResult::kSuccess,
      db_->PurgeMatchingOrigins(
          StorageKeyPolicyMatcherFunctionUtility::MakeMatcherFunction(
              {"http://www.example5.test"}),
          threshold2, base::Time::Max(), GetParam().perform_storage_cleanup));
}

TEST_P(SharedStorageDatabaseParamTest, PurgeStaleOrigins) {
  EXPECT_TRUE(db_->FetchOrigins().empty());

  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key2", u"value2"));
  EXPECT_EQ(2L, db_->Length(kOrigin1));
  EXPECT_EQ(db_->Get(kOrigin1, u"key1").data, u"value1");
  EXPECT_EQ(db_->Get(kOrigin1, u"key2").data, u"value2");

  const url::Origin kOrigin2 =
      url::Origin::Create(GURL("http://www.example2.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key1", u"value1"));
  EXPECT_EQ(1L, db_->Length(kOrigin2));
  EXPECT_EQ(db_->Get(kOrigin2, u"key1").data, u"value1");

  clock_.SetNow(base::Time::Now());
  clock_.Advance(base::Days(kOriginStalenessThresholdDays));
  clock_.Advance(base::Microseconds(1));

  const url::Origin kOrigin3 =
      url::Origin::Create(GURL("http://www.example3.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin3, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin3, u"key2", u"value2"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin3, u"key3", u"value3"));
  EXPECT_EQ(3L, db_->Length(kOrigin3));

  const url::Origin kOrigin4 =
      url::Origin::Create(GURL("http://www.example4.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin4, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin4, u"key2", u"value2"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin4, u"key3", u"value3"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin4, u"key4", u"value4"));
  EXPECT_EQ(4L, db_->Length(kOrigin4));

  std::vector<url::Origin> origins;
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(kOrigin1, kOrigin2, kOrigin3, kOrigin4));

  EXPECT_LT(db_->GetCreationTime(kOrigin1).time,
            db_->GetCreationTime(kOrigin3).time);
  EXPECT_LT(db_->GetCreationTime(kOrigin2).time,
            db_->GetCreationTime(kOrigin4).time);

  EXPECT_EQ(OperationResult::kSuccess, db_->PurgeStaleOrigins());

  // `kOrigin1` expired.
  EXPECT_EQ(0L, db_->Length(kOrigin1));

  // `kOrigin2` expired.
  EXPECT_EQ(0L, db_->Length(kOrigin2));

  // `kOrigin3` is active.
  EXPECT_EQ(3L, db_->Length(kOrigin3));

  // `kOrigin4` is active.
  EXPECT_EQ(4L, db_->Length(kOrigin4));

  origins.clear();
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(kOrigin3, kOrigin4));

  clock_.Advance(base::Days(kOriginStalenessThresholdDays));
  clock_.Advance(base::Microseconds(1));
  EXPECT_EQ(OperationResult::kSuccess, db_->PurgeStaleOrigins());

  // `kOrigin3` expired.
  EXPECT_EQ(0L, db_->Length(kOrigin3));

  // `kOrigin4` expired.
  EXPECT_EQ(0L, db_->Length(kOrigin4));

  EXPECT_TRUE(db_->FetchOrigins().empty());
}

TEST_P(SharedStorageDatabaseParamTest, TrimMemory) {
  EXPECT_TRUE(db_->FetchOrigins().empty());

  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key2", u"value2"));
  EXPECT_EQ(2L, db_->Length(kOrigin1));

  const url::Origin kOrigin2 =
      url::Origin::Create(GURL("http://www.example2.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin2, u"key1", u"value1"));
  EXPECT_EQ(1L, db_->Length(kOrigin2));

  const url::Origin kOrigin3 =
      url::Origin::Create(GURL("http://www.example3.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin3, u"key1", u"value1"));
  EXPECT_EQ(1L, db_->Length(kOrigin3));

  const url::Origin kOrigin4 =
      url::Origin::Create(GURL("http://www.example4.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin4, u"key1", u"value1"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin4, u"key2", u"value2"));
  EXPECT_EQ(2L, db_->Length(kOrigin4));

  std::vector<url::Origin> origins;
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(kOrigin1, kOrigin2, kOrigin3, kOrigin4));

  EXPECT_EQ(OperationResult::kSuccess, db_->Clear(kOrigin1));
  EXPECT_EQ(0L, db_->Length(kOrigin1));

  EXPECT_EQ(OperationResult::kSuccess, db_->Delete(kOrigin2, u"key1"));
  EXPECT_EQ(0L, db_->Length(kOrigin2));

  origins.clear();
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(kOrigin3, kOrigin4));

  // Release nonessential memory.
  db_->TrimMemory();

  // Check that the database is still intact.
  origins.clear();
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(kOrigin3, kOrigin4));

  EXPECT_EQ(1L, db_->Length(kOrigin3));
  EXPECT_EQ(2L, db_->Length(kOrigin4));

  EXPECT_EQ(db_->Get(kOrigin3, u"key1").data, u"value1");
  EXPECT_EQ(db_->Get(kOrigin4, u"key1").data, u"value1");
  EXPECT_EQ(db_->Get(kOrigin4, u"key2").data, u"value2");
}

TEST_P(SharedStorageDatabaseParamTest, MaxEntriesPerOrigin) {
  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", u"value1"));
  EXPECT_EQ(1L, db_->Length(kOrigin1));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key2", u"value2"));
  EXPECT_EQ(2L, db_->Length(kOrigin1));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key3", u"value3"));
  EXPECT_EQ(3L, db_->Length(kOrigin1));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key4", u"value4"));
  EXPECT_EQ(4L, db_->Length(kOrigin1));
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key5", u"value5"));
  EXPECT_EQ(5L, db_->Length(kOrigin1));

  // `kOrigin1` should have hit capacity, and hence this value will not be set.
  EXPECT_EQ(OperationResult::kNoCapacity,
            db_->Set(kOrigin1, u"key6", u"value6"));

  EXPECT_EQ(5L, db_->Length(kOrigin1));
  EXPECT_EQ(OperationResult::kSuccess, db_->Delete(kOrigin1, u"key5"));
  EXPECT_EQ(4L, db_->Length(kOrigin1));

  // There should now be capacity and the value will be set.
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key6", u"value6"));
  EXPECT_EQ(5L, db_->Length(kOrigin1));
}

TEST_P(SharedStorageDatabaseParamTest, MaxStringLength) {
  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example1.test"));
  const std::u16string kLongString(kMaxStringLength, u'g');

  // This value has the maximum allowed length.
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, u"key1", kLongString));
  EXPECT_EQ(1L, db_->Length(kOrigin1));

  // Appending to the value would exceed the allowed length and so won't
  // succeed.
  EXPECT_EQ(OperationResult::kInvalidAppend,
            db_->Append(kOrigin1, u"key1", u"h"));

  EXPECT_EQ(1L, db_->Length(kOrigin1));

  // This key has the maximum allowed length.
  EXPECT_EQ(OperationResult::kSet, db_->Set(kOrigin1, kLongString, u"value1"));
  EXPECT_EQ(2L, db_->Length(kOrigin1));
}

class SharedStorageDatabaseIteratorTest : public SharedStorageDatabaseTest {
 public:
  void SetUp() override {
    SharedStorageDatabaseTest::SetUp();

    auto options = SharedStorageOptions::Create()->GetDatabaseOptions();
    db_ = std::make_unique<SharedStorageDatabase>(
        file_name_, special_storage_policy_, std::move(options));
  }

  void InitSharedStorageFeature() override {
    scoped_feature_list_.InitAndEnableFeatureWithParameters(
        {blink::features::kSharedStorageAPI},
        {{"MaxSharedStorageEntriesPerOrigin",
          base::NumberToString(kMaxEntriesPerOriginForIteratorTest)},
         {"MaxSharedStorageStringLength",
          base::NumberToString(kMaxStringLength)},
         {"MaxSharedStorageIteratorBatchSize",
          base::NumberToString(kMaxBatchSizeForIteratorTest)}});
  }
};

TEST_F(SharedStorageDatabaseIteratorTest, Keys) {
  db_ = LoadFromFile("shared_storage.v1.iterator.sql");
  ASSERT_TRUE(db_);
  ASSERT_TRUE(db_->is_filebacked());

  url::Origin google_com = url::Origin::Create(GURL("http://google.com/"));
  TestSharedStorageEntriesListenerUtility utility(
      task_environment_.GetMainThreadTaskRunner());
  size_t id1 = utility.RegisterListener();
  EXPECT_EQ(OperationResult::kSuccess,
            db_->Keys(google_com, utility.BindNewPipeAndPassRemoteForId(id1)));
  utility.FlushForId(id1);
  EXPECT_EQ(201U, utility.TakeKeysForId(id1).size());

  // Batch size is 25 for this test.
  EXPECT_EQ(9U, utility.BatchCountForId(id1));
  utility.VerifyNoErrorForId(id1);

  url::Origin chromium_org = url::Origin::Create(GURL("http://chromium.org/"));
  size_t id2 = utility.RegisterListener();
  EXPECT_EQ(
      OperationResult::kSuccess,
      db_->Keys(chromium_org, utility.BindNewPipeAndPassRemoteForId(id2)));
  utility.FlushForId(id2);
  EXPECT_EQ(26U, utility.TakeKeysForId(id2).size());

  // Batch size is 25 for this test.
  EXPECT_EQ(2U, utility.BatchCountForId(id2));
  utility.VerifyNoErrorForId(id2);

  histogram_tester_.ExpectUniqueSample(kIsFileBackedHistogram, true, 1);
  histogram_tester_.ExpectUniqueSample(kFileSizeKBHistogram, 40, 1);
  histogram_tester_.ExpectUniqueSample(kNumOriginsHistogram, 2, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesTotalHistogram, 227, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMinHistogram, 26, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ1Histogram, 26, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMedianHistogram, 113.5, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ3Histogram, 201, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMaxHistogram, 201, 1);
}

TEST_F(SharedStorageDatabaseIteratorTest, Entries) {
  db_ = LoadFromFile("shared_storage.v1.iterator.sql");
  ASSERT_TRUE(db_);
  ASSERT_TRUE(db_->is_filebacked());

  url::Origin google_com = url::Origin::Create(GURL("http://google.com/"));
  TestSharedStorageEntriesListenerUtility utility(
      task_environment_.GetMainThreadTaskRunner());
  size_t id1 = utility.RegisterListener();
  EXPECT_EQ(
      OperationResult::kSuccess,
      db_->Entries(google_com, utility.BindNewPipeAndPassRemoteForId(id1)));
  utility.FlushForId(id1);
  EXPECT_EQ(201U, utility.TakeEntriesForId(id1).size());

  // Batch size is 25 for this test.
  EXPECT_EQ(9U, utility.BatchCountForId(id1));
  utility.VerifyNoErrorForId(id1);

  url::Origin chromium_org = url::Origin::Create(GURL("http://chromium.org/"));
  size_t id2 = utility.RegisterListener();
  EXPECT_EQ(
      OperationResult::kSuccess,
      db_->Entries(chromium_org, utility.BindNewPipeAndPassRemoteForId(id2)));
  utility.FlushForId(id2);
  EXPECT_EQ(26U, utility.TakeEntriesForId(id2).size());

  // Batch size is 25 for this test.
  EXPECT_EQ(2U, utility.BatchCountForId(id2));
  utility.VerifyNoErrorForId(id2);

  histogram_tester_.ExpectUniqueSample(kIsFileBackedHistogram, true, 1);
  histogram_tester_.ExpectUniqueSample(kFileSizeKBHistogram, 40, 1);
  histogram_tester_.ExpectUniqueSample(kNumOriginsHistogram, 2, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesTotalHistogram, 227, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMinHistogram, 26, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ1Histogram, 26, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMedianHistogram, 113.5, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ3Histogram, 201, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMaxHistogram, 201, 1);
}

// Tests correct calculation of five-number summary when there is only one
// origin.
TEST_F(SharedStorageDatabaseTest, SingleOrigin) {
  db_ = LoadFromFile("shared_storage.v1.single_origin.sql");
  ASSERT_TRUE(db_);
  ASSERT_TRUE(db_->is_filebacked());

  url::Origin google_com = url::Origin::Create(GURL("http://google.com/"));

  std::vector<url::Origin> origins;
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(google_com));

  histogram_tester_.ExpectUniqueSample(kIsFileBackedHistogram, true, 1);
  histogram_tester_.ExpectUniqueSample(kFileSizeKBHistogram, 29, 1);
  histogram_tester_.ExpectUniqueSample(kNumOriginsHistogram, 1, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesTotalHistogram, 10, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMinHistogram, 10, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ1Histogram, 10, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMedianHistogram, 10, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ3Histogram, 10, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMaxHistogram, 10, 1);
}

// Tests correct calculation of five-number summary when number of origins is
// greater than one and has remainder 1 modulo 4.
TEST_F(SharedStorageDatabaseTest, FiveOrigins) {
  db_ = LoadFromFile("shared_storage.v1.empty_values_mapping.5origins.sql");
  ASSERT_TRUE(db_);
  ASSERT_TRUE(db_->is_filebacked());

  url::Origin abc_xyz = url::Origin::Create(GURL("http://abc.xyz"));
  url::Origin chromium_org = url::Origin::Create(GURL("http://chromium.org/"));
  url::Origin google_com = url::Origin::Create(GURL("http://google.com/"));
  url::Origin google_org = url::Origin::Create(GURL("http://google.org/"));
  url::Origin gv_com = url::Origin::Create(GURL("http://gv.com"));

  std::vector<url::Origin> origins;
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(abc_xyz, chromium_org, google_com,
                                   google_org, gv_com));

  histogram_tester_.ExpectUniqueSample(kIsFileBackedHistogram, true, 1);
  histogram_tester_.ExpectUniqueSample(kFileSizeKBHistogram, 29, 1);
  histogram_tester_.ExpectUniqueSample(kNumOriginsHistogram, 5, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesTotalHistogram, 0, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMinHistogram, 10, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ1Histogram, 12.5, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMedianHistogram, 20, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ3Histogram, 145, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMaxHistogram, 250, 1);
}

// Tests correct calculation of five-number summary when number of origins has
// remainder 2 modulo 4.
TEST_F(SharedStorageDatabaseTest, SixOrigins) {
  db_ = LoadFromFile("shared_storage.v1.empty_values_mapping.6origins.sql");
  ASSERT_TRUE(db_);
  ASSERT_TRUE(db_->is_filebacked());

  url::Origin abc_xyz = url::Origin::Create(GURL("http://abc.xyz"));
  url::Origin chromium_org = url::Origin::Create(GURL("http://chromium.org/"));
  url::Origin google_com = url::Origin::Create(GURL("http://google.com/"));
  url::Origin google_org = url::Origin::Create(GURL("http://google.org/"));
  url::Origin gv_com = url::Origin::Create(GURL("http://gv.com"));
  url::Origin waymo_com = url::Origin::Create(GURL("http://waymo.com"));

  std::vector<url::Origin> origins;
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins, ElementsAre(abc_xyz, chromium_org, google_com,
                                   google_org, gv_com, waymo_com));

  histogram_tester_.ExpectUniqueSample(kIsFileBackedHistogram, true, 1);
  histogram_tester_.ExpectUniqueSample(kFileSizeKBHistogram, 29, 1);
  histogram_tester_.ExpectUniqueSample(kNumOriginsHistogram, 6, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesTotalHistogram, 0, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMinHistogram, 10, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ1Histogram, 15, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMedianHistogram, 30, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ3Histogram, 250, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMaxHistogram, 1599, 1);
}

// Tests correct calculation of five-number summary when number of origins has
// remainder 3 modulo 4.
TEST_F(SharedStorageDatabaseTest, SevenOrigins) {
  db_ = LoadFromFile("shared_storage.v1.empty_values_mapping.7origins.sql");
  ASSERT_TRUE(db_);
  ASSERT_TRUE(db_->is_filebacked());

  url::Origin abc_xyz = url::Origin::Create(GURL("http://abc.xyz"));
  url::Origin chromium_org = url::Origin::Create(GURL("http://chromium.org/"));
  url::Origin google_com = url::Origin::Create(GURL("http://google.com/"));
  url::Origin google_org = url::Origin::Create(GURL("http://google.org/"));
  url::Origin gv_com = url::Origin::Create(GURL("http://gv.com"));
  url::Origin waymo_com = url::Origin::Create(GURL("http://waymo.com"));
  url::Origin with_google_com =
      url::Origin::Create(GURL("http://withgoogle.com"));

  std::vector<url::Origin> origins;
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins,
              ElementsAre(abc_xyz, chromium_org, google_com, google_org, gv_com,
                          waymo_com, with_google_com));

  histogram_tester_.ExpectUniqueSample(kIsFileBackedHistogram, true, 1);
  histogram_tester_.ExpectUniqueSample(kFileSizeKBHistogram, 29, 1);
  histogram_tester_.ExpectUniqueSample(kNumOriginsHistogram, 7, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesTotalHistogram, 0, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMinHistogram, 10, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ1Histogram, 15, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMedianHistogram, 40, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ3Histogram, 1001, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMaxHistogram, 1599, 1);
}

// Tests correct calculation of five-number summary when number of origins has
// remainder 0 modulo 4.
TEST_F(SharedStorageDatabaseTest, EightOrigins) {
  db_ = LoadFromFile("shared_storage.v1.empty_values_mapping.8origins.sql");
  ASSERT_TRUE(db_);
  ASSERT_TRUE(db_->is_filebacked());

  url::Origin abc_xyz = url::Origin::Create(GURL("http://abc.xyz"));
  url::Origin chromium_org = url::Origin::Create(GURL("http://chromium.org/"));
  url::Origin google_com = url::Origin::Create(GURL("http://google.com/"));
  url::Origin google_org = url::Origin::Create(GURL("http://google.org/"));
  url::Origin gv_com = url::Origin::Create(GURL("http://gv.com"));
  url::Origin waymo_com = url::Origin::Create(GURL("http://waymo.com"));
  url::Origin with_google_com =
      url::Origin::Create(GURL("http://withgoogle.com"));
  url::Origin youtube_com = url::Origin::Create(GURL("http://youtube.com/"));

  std::vector<url::Origin> origins;
  for (const auto& info : db_->FetchOrigins())
    origins.push_back(info->storage_key.origin());
  EXPECT_THAT(origins,
              ElementsAre(abc_xyz, chromium_org, google_com, google_org, gv_com,
                          waymo_com, with_google_com, youtube_com));

  histogram_tester_.ExpectUniqueSample(kIsFileBackedHistogram, true, 1);
  histogram_tester_.ExpectUniqueSample(kFileSizeKBHistogram, 29, 1);
  histogram_tester_.ExpectUniqueSample(kNumOriginsHistogram, 8, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesTotalHistogram, 0, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMinHistogram, 10, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ1Histogram, 17.5, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMedianHistogram, 70, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesQ3Histogram, 625.5, 1);
  histogram_tester_.ExpectUniqueSample(kNumEntriesMaxHistogram, 1599, 1);
}

}  // namespace storage
