// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/path_service.h"
#include "gtest/gtest.h"

#include "puffin/memory_stream.h"
#include "puffin/src/include/puffin/common.h"
#include "puffin/src/include/puffin/puffdiff.h"
#include "puffin/src/include/puffin/puffpatch.h"
#include "puffin/src/include/puffin/utils.h"
#include "puffin/src/logging.h"
#include "puffin/src/puffin_stream.h"
#include "puffin/src/unittest_common.h"

#define PRINT_SAMPLE 0  // Set to 1 if you want to print the generated samples.

using std::string;
using std::vector;

namespace puffin {

namespace {

base::FilePath out_test_file(const char* file) {
  base::FilePath path;
  base::PathService::Get(base::DIR_GEN_TEST_DATA_ROOT, &path);
  return path.AppendASCII(file);
}

#if PRINT_SAMPLE
// Print an array into hex-format to the output. This can be used to create
// static arrays for unit testing of the puffer/huffer.
void PrintArray(const string& name, const Buffer& array) {
  std::cout << "const Buffer " << name << " = {" << std::endl << " ";
  for (size_t idx = 0; idx < array.size(); idx++) {
    std::cout << " 0x" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << uint(array[idx]);
    if (idx == array.size() - 1) {
      std::cout << std::dec << "};" << std::endl;
      return;
    }
    std::cout << ",";
    if ((idx + 1) % 12 == 0) {
      std::cout << std::endl << " ";
    }
  }
}
#endif

const Buffer kPatch1To2 = {
    0x50, 0x55, 0x46, 0x31, 0x00, 0x00, 0x00, 0x53, 0x08, 0x01, 0x12, 0x27,
    0x0A, 0x04, 0x08, 0x10, 0x10, 0x32, 0x0A, 0x04, 0x08, 0x50, 0x10, 0x0A,
    0x0A, 0x04, 0x08, 0x60, 0x10, 0x12, 0x12, 0x04, 0x08, 0x10, 0x10, 0x58,
    0x12, 0x04, 0x08, 0x78, 0x10, 0x28, 0x12, 0x05, 0x08, 0xA8, 0x01, 0x10,
    0x38, 0x18, 0x1F, 0x1A, 0x24, 0x0A, 0x02, 0x10, 0x32, 0x0A, 0x04, 0x08,
    0x48, 0x10, 0x50, 0x0A, 0x05, 0x08, 0x98, 0x01, 0x10, 0x12, 0x12, 0x02,
    0x10, 0x58, 0x12, 0x04, 0x08, 0x70, 0x10, 0x58, 0x12, 0x05, 0x08, 0xC8,
    0x01, 0x10, 0x38, 0x18, 0x21, 0x20, 0x01, 0x17, 0x68, 0x00, 0x00, 0xC4,
    0xCA, 0xB3, 0xBD, 0x3C, 0x18, 0x8B, 0x67, 0x42, 0x10, 0xD0, 0x14, 0xDF,
    0x4F, 0xFB, 0x7B, 0x4B, 0x56, 0x93, 0x72, 0x85, 0xEB, 0xF5, 0x42, 0x2E,
    0x0E, 0x78, 0x60, 0x13, 0x9D, 0xEB, 0x6B, 0x97, 0x48, 0x96, 0x68, 0x8E,
    0x79, 0x8C, 0x65, 0x19, 0x8E, 0x84, 0x4C, 0xF6, 0xB6, 0x25, 0x00, 0x66,
    0x9F, 0xCA, 0xDD, 0x56, 0xF3, 0x86, 0xD9, 0xD7, 0x37, 0x36, 0x0C, 0xD8,
    0x8F, 0xC1, 0x04, 0x4B, 0x5C, 0x08, 0x52, 0xFA, 0x1F, 0x80, 0x43, 0x10,
    0xE3, 0x42, 0xFE, 0x27, 0x70, 0x81, 0x78, 0x19};

const Buffer kPatch2To1 = {
    0x50, 0x55, 0x46, 0x31, 0x00, 0x00, 0x00, 0x53, 0x08, 0x01, 0x12, 0x24,
    0x0A, 0x02, 0x10, 0x32, 0x0A, 0x04, 0x08, 0x48, 0x10, 0x50, 0x0A, 0x05,
    0x08, 0x98, 0x01, 0x10, 0x12, 0x12, 0x02, 0x10, 0x58, 0x12, 0x04, 0x08,
    0x70, 0x10, 0x58, 0x12, 0x05, 0x08, 0xC8, 0x01, 0x10, 0x38, 0x18, 0x21,
    0x1A, 0x27, 0x0A, 0x04, 0x08, 0x10, 0x10, 0x32, 0x0A, 0x04, 0x08, 0x50,
    0x10, 0x0A, 0x0A, 0x04, 0x08, 0x60, 0x10, 0x12, 0x12, 0x04, 0x08, 0x10,
    0x10, 0x58, 0x12, 0x04, 0x08, 0x78, 0x10, 0x28, 0x12, 0x05, 0x08, 0xA8,
    0x01, 0x10, 0x38, 0x18, 0x1F, 0x20, 0x01, 0x17, 0x66, 0x00, 0x00, 0xC4,
    0xE7, 0xBD, 0xF5, 0x37, 0xBF, 0x41, 0xB6, 0x12, 0xCF, 0xE2, 0xA2, 0xF3,
    0x78, 0xDE, 0xFD, 0x3C, 0xB2, 0x99, 0xC4, 0x09, 0xE7, 0x79, 0x50, 0x93,
    0xF3, 0xA8, 0xC0, 0x12, 0xCD, 0xF5, 0xE2, 0xE6, 0xAF, 0x59, 0xA8, 0x79,
    0x0E, 0x73, 0x39, 0x28, 0xD2, 0x56, 0xCB, 0x73, 0x9A, 0x08, 0x00, 0xEF,
    0xDE, 0xAC, 0xFE, 0x8C, 0xF6, 0xE9, 0x83, 0xFB, 0x3E, 0xF8, 0x51, 0x20,
    0xA3, 0x39, 0x5E, 0x9A, 0x0C, 0xA3, 0xC1, 0xB4, 0x2B, 0x5F, 0xEE, 0xBF,
    0x2C, 0x07, 0x92, 0x2F, 0x4C, 0x50};

const Buffer kPatch1ToNoDeflate = {
    0x50, 0x55, 0x46, 0x31, 0x00, 0x00, 0x00, 0x31, 0x08, 0x01, 0x12, 0x27,
    0x0A, 0x04, 0x08, 0x10, 0x10, 0x32, 0x0A, 0x04, 0x08, 0x50, 0x10, 0x0A,
    0x0A, 0x04, 0x08, 0x60, 0x10, 0x12, 0x12, 0x04, 0x08, 0x10, 0x10, 0x58,
    0x12, 0x04, 0x08, 0x78, 0x10, 0x28, 0x12, 0x05, 0x08, 0xA8, 0x01, 0x10,
    0x38, 0x18, 0x1F, 0x1A, 0x02, 0x18, 0x04, 0x20, 0x01, 0x17, 0x55, 0x00,
    0x00, 0x04, 0x22, 0x77, 0xF7, 0x5B, 0x96, 0xA7, 0x2D, 0x0F, 0x34, 0xD0,
    0x8A, 0xA3, 0xB6, 0x9F, 0x97, 0xA4, 0x10, 0x41, 0x1C, 0x60, 0x64, 0x79,
    0xC0, 0xDD, 0x89, 0xF7, 0xEB, 0xCD, 0x38, 0x73, 0xAE, 0x81, 0x87, 0x6C,
    0x17, 0x14, 0x45, 0x80, 0xDB, 0x52, 0x15, 0xC0, 0xF7, 0xF6, 0x74, 0x4C,
    0xF2, 0xFE, 0xAE, 0xBB, 0xF9, 0x54, 0x02, 0x79, 0x94, 0x39, 0x7E, 0xD6,
    0x85, 0x88, 0x00};

}  // namespace

void TestPatching(const Buffer& src_buf,
                  const Buffer& dst_buf,
                  const vector<BitExtent>& src_deflates,
                  const vector<BitExtent>& dst_deflates,
                  const Buffer patch) {
  Buffer patch_out;
  string patch_path;
  ASSERT_TRUE(MakeTempFile(&patch_path));
  ScopedPathUnlinker scoped_unlinker(patch_path);
  ASSERT_TRUE(PuffDiff(src_buf, dst_buf, src_deflates, dst_deflates,
                       {puffin::CompressorType::kBrotli}, patch_path,
                       &patch_out));

#if PRINT_SAMPLE
  PrintArray("kPatchIn", patch);
  PrintArray("kPatchOut", patch_out);
#endif

  EXPECT_EQ(patch_out, patch);

  auto src_stream = MemoryStream::CreateForRead(src_buf);
  Buffer dst_buf_out(dst_buf.size());
  auto dst_stream = MemoryStream::CreateForWrite(&dst_buf_out);
  ASSERT_EQ(PuffPatch(std::move(src_stream), std::move(dst_stream),
                      patch.data(), patch.size()),
            Status::P_OK);
  EXPECT_EQ(dst_buf_out, dst_buf);
}

TEST(PatchingTest, Patching1To2Test) {
  TestPatching(kDeflatesSample1, kDeflatesSample2,
               kSubblockDeflateExtentsSample1, kSubblockDeflateExtentsSample2,
               kPatch1To2);
}

TEST(PatchingTest, Patching2To1Test) {
  TestPatching(kDeflatesSample2, kDeflatesSample1,
               kSubblockDeflateExtentsSample2, kSubblockDeflateExtentsSample1,
               kPatch2To1);
}

TEST(PatchingTest, Patching1ToNoDeflateTest) {
  TestPatching(kDeflatesSample1, {11, 22, 33, 44},
               kSubblockDeflateExtentsSample1, {}, kPatch1ToNoDeflate);
}

TEST(PatchingTest, ApplyPuffPatchTest) {
  base::FilePath app_v1_crx = out_test_file("puffin_app_v1.crx3");
  base::FilePath app_v2_crx = out_test_file("puffin_app_v2.crx3");
  base::FilePath patch_v1_to_v2_puff =
      out_test_file("puffin_app_v1_to_v2.puff");
  base::FilePath patch_v2_to_v1_puff =
      out_test_file("puffin_app_v2_to_v1.puff");
  base::FilePath app_v1_to_v2_crx = out_test_file("puffin_app_v1_to_v2.crx3");
  base::FilePath app_v2_to_v1_crx = out_test_file("puffin_app_v2_to_v1.crx3");

  // Test patching v1 to v2:
  ASSERT_TRUE(base::DeleteFile(app_v1_to_v2_crx));
  ASSERT_EQ(ApplyPuffPatch(app_v1_crx, patch_v1_to_v2_puff, app_v1_to_v2_crx),
            Status::P_OK);
  EXPECT_TRUE(base::ContentsEqual(app_v2_crx, app_v1_to_v2_crx));

  // Test patching v2 to v1:
  ASSERT_TRUE(base::DeleteFile(app_v2_to_v1_crx));
  ASSERT_EQ(ApplyPuffPatch(app_v2_crx, patch_v2_to_v1_puff, app_v2_to_v1_crx),
            Status::P_OK);
  EXPECT_TRUE(base::ContentsEqual(app_v1_crx, app_v2_to_v1_crx));
}

TEST(PatchingTest, PuffDiffTest) {
  base::FilePath app_v1_crx = out_test_file("puffin_app_v1.crx3");
  base::FilePath app_v2_crx = out_test_file("puffin_app_v2.crx3");
  base::FilePath expected_patch_v1_to_v2_puff =
      out_test_file("puffin_app_v1_to_v2.puff");
  base::FilePath expected_patch_v2_to_v1_puff =
      out_test_file("puffin_app_v2_to_v1.puff");
  base::FilePath actual_patch_v1_to_v2_puff =
      out_test_file("actual_puffin_app_v1_to_v2.puff");
  base::FilePath actual_patch_v2_to_v1_puff =
      out_test_file("actual_puffin_app_v2_to_v1.puff");
  // Test patching v1 to v2:
  ASSERT_TRUE(base::DeleteFile(actual_patch_v1_to_v2_puff));
  ASSERT_EQ(PuffDiff(app_v1_crx.MaybeAsASCII(), app_v2_crx.MaybeAsASCII(),
                     actual_patch_v1_to_v2_puff.MaybeAsASCII()),
            Status::P_OK);
  EXPECT_TRUE(base::ContentsEqual(expected_patch_v1_to_v2_puff,
                                  actual_patch_v1_to_v2_puff));

  // Test patching v2 to v1:
  ASSERT_TRUE(base::DeleteFile(actual_patch_v2_to_v1_puff));
  ASSERT_EQ(PuffDiff(app_v2_crx.MaybeAsASCII(), app_v1_crx.MaybeAsASCII(),
                     actual_patch_v2_to_v1_puff.MaybeAsASCII()),
            Status::P_OK);
  EXPECT_TRUE(base::ContentsEqual(expected_patch_v2_to_v1_puff,
                                  actual_patch_v2_to_v1_puff));
}

// TODO(ahassani): add tests for:
//   TestPatchingNoDeflateTo2

// TODO(ahassani): Change tests data if you decided to compress the header of
// the patch.

}  // namespace puffin