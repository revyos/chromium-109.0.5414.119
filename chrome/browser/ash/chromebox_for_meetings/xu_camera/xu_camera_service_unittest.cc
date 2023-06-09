// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/chromebox_for_meetings/xu_camera/xu_camera_service.h"

#include <asm-generic/errno.h>
#include <linux/uvcvideo.h>
#include <linux/videodev2.h>
#include <cstdint>

#include "base/bind.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/mock_callback.h"
#include "base/test/task_environment.h"
#include "chromeos/ash/components/dbus/chromebox_for_meetings/fake_cfm_hotline_client.h"
#include "chromeos/ash/services/chromebox_for_meetings/public/cpp/fake_service_connection.h"
#include "chromeos/ash/services/chromebox_for_meetings/public/cpp/fake_service_context.h"
#include "chromeos/ash/services/chromebox_for_meetings/public/cpp/service_connection.h"
#include "chromeos/ash/services/chromebox_for_meetings/public/mojom/xu_camera.mojom.h"
#include "content/public/test/test_utils.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace ash::cfm {
namespace {

const std::vector<uint8_t> kGuid = {0x12, 0x34, 0x12, 0x34, 0x12, 0x34,
                                    0x12, 0x34, 0x12, 0x34, 0x12, 0x34,
                                    0x12, 0x34, 0x12, 0x34};
const auto kWebcamId = mojom::WebcamId::NewDevPath("/fake/device/path");
const mojom::CtrlTypePtr kCtrlType =
    mojom::CtrlType::NewQueryCtrl(mojom::ControlQuery::New(1, 1));
const auto kMenuEntries = mojom::MenuEntries::New();
const std::vector<uint8_t> kName(32, 'a');
const std::vector<uint8_t> kData = {0x43, 0x21};

class XuCameraServiceTest : public ::testing::Test {
 public:
  XuCameraServiceTest() = default;
  XuCameraServiceTest(const XuCameraServiceTest&) = delete;
  XuCameraServiceTest& operator=(const XuCameraServiceTest&) = delete;

  void SetUp() override {
    CfmHotlineClient::InitializeFake();
    ServiceConnection::UseFakeServiceConnectionForTesting(
        &fake_service_connection_);
    XuCameraService::Initialize();
  }

  void TearDown() override {
    XuCameraService::Shutdown();
    CfmHotlineClient::Shutdown();
  }

  FakeCfmHotlineClient* GetClient() {
    return static_cast<FakeCfmHotlineClient*>(CfmHotlineClient::Get());
  }

  // Returns a mojo::Remote for the mojom::XuCamera by faking the
  // way the cfm mojom binder daemon would request it through chrome.
  const mojo::Remote<mojom::XuCamera>& GetXuCameraRemote() {
    if (xu_camera_remote_.is_bound()) {
      return xu_camera_remote_;
    }

    // if there is no valid remote create one
    auto* interface_name = mojom::XuCamera::Name_;

    base::RunLoop run_loop;

    // Fake out CfmServiceContext
    fake_service_connection_.SetCallback(base::BindLambdaForTesting(
        [&](mojo::PendingReceiver<chromeos::cfm::mojom::CfmServiceContext>
                pending_receiver,
            bool success) {
          ASSERT_TRUE(success);
          context_receiver_set_.Add(&context_, std::move(pending_receiver));
        }));

    context_.SetFakeProvideAdaptorCallback(base::BindLambdaForTesting(
        [&](const std::string& service_id,
            mojo::PendingRemote<chromeos::cfm::mojom::CfmServiceAdaptor>
                pending_adaptor_remote,
            chromeos::cfm::mojom::CfmServiceContext::ProvideAdaptorCallback
                callback) {
          ASSERT_EQ(interface_name, service_id);
          adaptor_remote_.Bind(std::move(pending_adaptor_remote));
          std::move(callback).Run(true);
        }));

    EXPECT_TRUE(GetClient()->FakeEmitSignal(interface_name));
    run_loop.RunUntilIdle();

    EXPECT_TRUE(adaptor_remote_.is_connected());

    adaptor_remote_->OnBindService(
        xu_camera_remote_.BindNewPipeAndPassReceiver().PassPipe());
    EXPECT_TRUE(xu_camera_remote_.is_connected());

    return xu_camera_remote_;
  }

 protected:
  FakeCfmServiceContext context_;
  mojo::Remote<mojom::XuCamera> xu_camera_remote_;
  mojo::ReceiverSet<chromeos::cfm::mojom::CfmServiceContext>
      context_receiver_set_;
  mojo::Remote<chromeos::cfm::mojom::CfmServiceAdaptor> adaptor_remote_;
  FakeServiceConnectionImpl fake_service_connection_;
  base::test::SingleThreadTaskEnvironment task_environment_;
};

// This test ensures that the XuCameraService is discoverable by its
// mojom name by sending a signal received by CfmHotlineClient.
TEST_F(XuCameraServiceTest, XuCameraServiceAvailable) {
  ASSERT_TRUE(GetClient()->FakeEmitSignal(mojom::XuCamera::Name_));
}

// This test ensures that the XuCameraService correctly registers itself
// for discovery by the cfm mojom binder daemon and correctly returns a
// working mojom remote.
TEST_F(XuCameraServiceTest, GetXuCameraRemote) {
  ASSERT_TRUE(GetXuCameraRemote().is_connected());
}

// This test ensure that the XU camera can get unit id
TEST_F(XuCameraServiceTest, GetXuCameraUnitId) {
  base::RunLoop run_loop;
  GetXuCameraRemote()->GetUnitId(
      /* id= */ kWebcamId->Clone(), /* guid= */ kGuid,
      base::BindLambdaForTesting(
          [&](const uint8_t error_code, const uint8_t unit_id) {
            EXPECT_EQ(error_code, ENOSYS);
            EXPECT_EQ(unit_id, '0');
            run_loop.Quit();
          }));
  run_loop.Run();
}

// This test ensure that the XU camera can map control
TEST_F(XuCameraServiceTest, GetXuCameraMapCtrl) {
  auto mapping = mojom::ControlMapping::New(
      /* id= */ 1, /* name= */ kName, /* guid= */ kGuid, /* selector= */ 1,
      /* size= */ 1, /* offset= */ 1, /* v4l2_type= */ V4L2_CTRL_TYPE_INTEGER,
      /* data_type= */ UVC_CTRL_DATA_TYPE_SIGNED, /* menu_entries= */
      kMenuEntries->Clone());
  base::RunLoop run_loop;
  GetXuCameraRemote()->MapCtrl(
      /* id= */ kWebcamId->Clone(), /* mapping_ctrl= */ mapping->Clone(),
      base::BindLambdaForTesting([&](const uint8_t error_code) {
        EXPECT_EQ(error_code, ENOSYS);
        run_loop.Quit();
      }));
  run_loop.Run();
}

/// This test ensure that the XU camera can get control given a ctrl query
TEST_F(XuCameraServiceTest, GetXuCameraGetCtrl) {
  base::RunLoop run_loop;
  GetXuCameraRemote()->GetCtrl(
      /* id= */ kWebcamId->Clone(), /* ctrl= */ kCtrlType->Clone(),
      /* fn= */ mojom::GetFn::kCur,
      base::BindLambdaForTesting(
          [&](const uint8_t error_code, const std::vector<uint8_t>& data) {
            const std::vector<uint8_t> vec;
            EXPECT_EQ(error_code, ENOSYS);
            EXPECT_EQ(data, vec);
            run_loop.Quit();
          }));
  run_loop.Run();
}

// This test ensure that the XU camera can set control
TEST_F(XuCameraServiceTest, GetXuCameraSetCtrl) {
  base::RunLoop run_loop;
  GetXuCameraRemote()->SetCtrl(
      /* id= */ kWebcamId->Clone(),
      /* ctrl= */ kCtrlType->Clone(),
      /* data= */ kData,
      base::BindLambdaForTesting([&](const uint8_t error_code) {
        EXPECT_EQ(error_code, ENOSYS);
        run_loop.Quit();
      }));
  run_loop.Run();
}

}  // namespace
}  // namespace ash::cfm
