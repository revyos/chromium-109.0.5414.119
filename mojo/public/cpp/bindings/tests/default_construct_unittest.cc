// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/tests/default_construct_unittest.test-mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo::test::default_construct {

class TestInterface : public mojom::TestInterface {
 public:
  explicit TestInterface(mojo::PendingReceiver<mojom::TestInterface> receiver)
      : receiver_(this, std::move(receiver)) {}

  void TestMethod(const TestStruct& in, TestMethodCallback callback) override {
    std::move(callback).Run(in);
  }

 private:
  mojo::Receiver<mojom::TestInterface> receiver_;
};

class DefaultConstructTest : public ::testing::Test {
 private:
  base::test::SingleThreadTaskEnvironment task_environment_;
};

TEST_F(DefaultConstructTest, Echo) {
  mojo::Remote<mojom::TestInterface> remote;
  TestInterface instance(remote.BindNewPipeAndPassReceiver());

  base::RunLoop run_loop;
  remote->TestMethod(TestStruct(42),
                     base::BindLambdaForTesting([&](const TestStruct& out) {
                       EXPECT_EQ(out.value, 42);
                       run_loop.Quit();
                     }));
}

}  // namespace mojo::test::default_construct
