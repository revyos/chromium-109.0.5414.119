// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/quick_pair/common/fast_pair/fast_pair_metrics.h"

#include "ash/quick_pair/common/device.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/sparse_histogram.h"

namespace {

// Error strings should be kept in sync with the strings reflected in
// device/bluetooth/bluez/bluetooth_socket_bluez.cc.
const char kAcceptFailedString[] = "Failed to accept connection.";
const char kInvalidUUIDString[] = "Invalid UUID";
const char kSocketNotListeningString[] = "Socket is not listening.";

// Top Popular peripherals and first party devices. These device
// model names should be kept in sync with the FastPairTrackedModelID
// enum in src/tools/metrics/histograms/enums.xml. Devices may have multiple
// Model IDs associated with the same device (for example, each Pixel Bud Pros
// have different Model IDs for each different color) so we append '_*' to the
// naming for subsequent Model IDs after the first one.
const char kPopularPeripheral_JBLLIVEPROTWS_ModelId[] = "461BB8";
const char kPopularPeripheral_JBLLIVEPROTWS_Name[] = "JBLLIVEPROTWS";
const char kPopularPeripheral_JBLLIVEPROTWS_1_ModelId[] = "C6936A";
const char kPopularPeripheral_JBLLIVEPROTWS_1_Name[] = "JBLLIVEPROTWS_1";
const char kPopularPeripheral_JBLLIVEPROTWS_2_ModelId[] = "F52494";
const char kPopularPeripheral_JBLLIVEPROTWS_2_Name[] = "JBLLIVEPROTWS_2";
const char kPopularPeripheral_JBLLIVEPROTWS_3_ModelId[] = "15BA5F";
const char kPopularPeripheral_JBLLIVEPROTWS_3_Name[] = "JBLLIVEPROTWS_3";
const char kPopularPeripheral_JBLLIVEPROTWS_4_ModelId[] = "56DB24";
const char kPopularPeripheral_JBLLIVEPROTWS_4_Name[] = "JBLLIVEPROTWS_4";
const char kPopularPeripheral_JBLLIVEPROTWS_5_ModelId[] = "8CB05C";
const char kPopularPeripheral_JBLLIVEPROTWS_5_Name[] = "JBLLIVEPROTWS_5";
const char kPopularPeripheral_JBLLIVEPROTWS_6_ModelId[] = "F8013A";
const char kPopularPeripheral_JBLLIVEPROTWS_6_Name[] = "JBLLIVEPROTWS_6";

const char kPopularPeripheral_JBLLIVE300TWS_ModelId[] = "718FA4";
const char kPopularPeripheral_JBLLIVE300TWS_Name[] = "JBLLIVE300TWS";
const char kPopularPeripheral_JBLLIVE300TWS_1_ModelId[] = "7C1C37";
const char kPopularPeripheral_JBLLIVE300TWS_1_Name[] = "JBLLIVE300TWS_1";
const char kPopularPeripheral_JBLLIVE300TWS_2_ModelId[] = "2A35AD";
const char kPopularPeripheral_JBLLIVE300TWS_2_Name[] = "JBLLIVE300TWS_2";

const char kPopularPeripheral_JBLLIVE400BT_ModelId[] = "F00209";
const char kPopularPeripheral_JBLLIVE400BT_Name[] = "JBLLIVE400BT";
const char kPopularPeripheral_JBLLIVE400BT_1_ModelId[] = "F0020B";
const char kPopularPeripheral_JBLLIVE400BT_1_Name[] = "JBLLIVE400BT_1";
const char kPopularPeripheral_JBLLIVE400BT_2_ModelId[] = "F0020C";
const char kPopularPeripheral_JBLLIVE400BT_2_Name[] = "JBLLIVE400BT_2";
const char kPopularPeripheral_JBLLIVE400BT_3_ModelId[] = "F0020D";
const char kPopularPeripheral_JBLLIVE400BT_3_Name[] = "JBLLIVE400BT_3";
const char kPopularPeripheral_JBLLIVE400BT_4_ModelId[] = "F0020A";
const char kPopularPeripheral_JBLLIVE400BT_4_Name[] = "JBLLIVE400BT_4";

const char kPopularPeripheral_JBLTUNE125TWS_ModelId[] = "FF1B63";
const char kPopularPeripheral_JBLTUNE125TWS_Name[] = "JBLTUNE125TWS";
const char kPopularPeripheral_JBLTUNE125TWS_1_ModelId[] = "054B2D";
const char kPopularPeripheral_JBLTUNE125TWS_1_Name[] = "JBLTUNE125TWS_1";
const char kPopularPeripheral_JBLTUNE125TWS_2_ModelId[] = "D97EBA";
const char kPopularPeripheral_JBLTUNE125TWS_2_Name[] = "JBLTUNE125TWS_2";
const char kPopularPeripheral_JBLTUNE125TWS_3_ModelId[] = "565EAA";
const char kPopularPeripheral_JBLTUNE125TWS_3_Name[] = "JBLTUNE125TWS_3";
const char kPopularPeripheral_JBLTUNE125TWS_4_ModelId[] = "E1DD91";
const char kPopularPeripheral_JBLTUNE125TWS_4_Name[] = "JBLTUNE125TWS_4";
const char kPopularPeripheral_JBLTUNE125TWS_5_ModelId[] = "BD193B";
const char kPopularPeripheral_JBLTUNE125TWS_5_Name[] = "JBLTUNE125TWS_5";

const char kPopularPeripheral_JBLTUNE225TWS_ModelId[] = "5C0C84";
const char kPopularPeripheral_JBLTUNE225TWS_Name[] = "JBLTUNE225TWS";
const char kPopularPeripheral_JBLTUNE225TWS_1_ModelId[] = "FAA6C3";
const char kPopularPeripheral_JBLTUNE225TWS_1_Name[] = "JBLTUNE225TWS_1";
const char kPopularPeripheral_JBLTUNE225TWS_2_ModelId[] = "9BC64D";
const char kPopularPeripheral_JBLTUNE225TWS_2_Name[] = "JBLTUNE225TWS_2";
const char kPopularPeripheral_JBLTUNE225TWS_3_ModelId[] = "B8393A";
const char kPopularPeripheral_JBLTUNE225TWS_3_Name[] = "JBLTUNE225TWS_3";
const char kPopularPeripheral_JBLTUNE225TWS_4_ModelId[] = "5BD6C9";
const char kPopularPeripheral_JBLTUNE225TWS_4_Name[] = "JBLTUNE225TWS_4";
const char kPopularPeripheral_JBLTUNE225TWS_5_ModelId[] = "9C98DB";
const char kPopularPeripheral_JBLTUNE225TWS_5_Name[] = "JBLTUNE225TWS_5";

const char kPopularPeripheral_JBLTUNE130NCTWS_ModelId[] = "BDB433";
const char kPopularPeripheral_JBLTUNE130NCTWS_Name[] = "JBLTUNE130NCTWS";
const char kPopularPeripheral_JBLTUNE130NCTWS_1_ModelId[] = "1115E7";
const char kPopularPeripheral_JBLTUNE130NCTWS_1_Name[] = "JBLTUNE130NCTWS_1";
const char kPopularPeripheral_JBLTUNE130NCTWS_2_ModelId[] = "436FD1";
const char kPopularPeripheral_JBLTUNE130NCTWS_2_Name[] = "JBLTUNE130NCTWS_2";
const char kPopularPeripheral_JBLTUNE130NCTWS_3_ModelId[] = "B73DBA";
const char kPopularPeripheral_JBLTUNE130NCTWS_3_Name[] = "JBLTUNE130NCTWS_3";

const char kPopularPeripheral_NothingEar1_ModelId[] = "31D53D";
const char kPopularPeripheral_NothingEar1_Name[] = "NOTHINGEAR1";
const char kPopularPeripheral_NothingEar1_1_ModelId[] = "624011";
const char kPopularPeripheral_NothingEar1_1_Name[] = "NOTHINGEAR1_1";

const char kPopularPeripheral_OnePlusBudsZ_ModelId[] = "A41C91";
const char kPopularPeripheral_OnePlusBudsZ_Name[] = "OnePlusBudsZ";
const char kPopularPeripheral_OnePlusBudsZ_1_ModelId[] = "1393DE";
const char kPopularPeripheral_OnePlusBudsZ_1_Name[] = "OnePlusBudsZ_1";
const char kPopularPeripheral_OnePlusBudsZ_2_ModelId[] = "E07634";
const char kPopularPeripheral_OnePlusBudsZ_2_Name[] = "OnePlusBudsZ_2";

const char kPopularPeripheral_PixelBuds_ModelId[] = "060000";
const char kPopularPeripheral_PixelBuds_Name[] = "PixelBuds";

const char kPopularPeripheral_PixelBudsASeries_ModelId[] = "718C17";
const char kPopularPeripheral_PixelBudsASeries_Name[] = "PixelBudsASeries";
const char kPopularPeripheral_PixelBudsASeries_1_ModelId[] = "8B66AB";
const char kPopularPeripheral_PixelBudsASeries_1_Name[] = "PixelBudsASeries_1";
const char kPopularPeripheral_PixelBudsASeries_2_ModelId[] = "3E7540";
const char kPopularPeripheral_PixelBudsASeries_2_Name[] = "PixelBudsASeries_2";

const char kPopularPeripheral_PixelBudsPro_ModelId[] = "F2020E";
const char kPopularPeripheral_PixelBudsPro_Name[] = "PixelBudsPro";
const char kPopularPeripheral_PixelBudsPro_1_ModelId[] = "6EDAF7";
const char kPopularPeripheral_PixelBudsPro_1_Name[] = "PixelBudsPro_1";
const char kPopularPeripheral_PixelBudsPro_2_ModelId[] = "5A36A5";
const char kPopularPeripheral_PixelBudsPro_2_Name[] = "PixelBudsPro_2";
const char kPopularPeripheral_PixelBudsPro_3_ModelId[] = "F58DE7";
const char kPopularPeripheral_PixelBudsPro_3_Name[] = "PixelBudsPro_3";
const char kPopularPeripheral_PixelBudsPro_4_ModelId[] = "9ADB11";
const char kPopularPeripheral_PixelBudsPro_4_Name[] = "PixelBudsPro_4";

const char kPopularPeripheral_RealMeBudsAirPro_ModelId[] = "8CD10F";
const char kPopularPeripheral_RealMeBudsAirPro_Name[] = "RealMeBudsAirPro";
const char kPopularPeripheral_RealMeBudsAirPro_1_ModelId[] = "A6E1A6";
const char kPopularPeripheral_RealMeBudsAirPro_1_Name[] = "RealMeBudsAirPro_1";
const char kPopularPeripheral_RealMeBudsAirPro_2_ModelId[] = "2F208E";
const char kPopularPeripheral_RealMeBudsAirPro_2_Name[] = "RealMeBudsAirPro_2";

const char kPopularPeripheral_RealMeBudsAir2_ModelId[] = "BA5D56";
const char kPopularPeripheral_RealMeBudsAir2_Name[] = "RealMeBudsAir2";

const char kPopularPeripheral_RealMeBudsAir2Neo_ModelId[] = "0B5374";
const char kPopularPeripheral_RealMeBudsAir2Neo_Name[] = "RealMeBudsAir2Neo";

const char kPopularPeripheral_SonyWF1000XM3_ModelId[] = "38C95C";
const char kPopularPeripheral_SonyWF1000XM3_Name[] = "SonyWF1000XM3";
const char kPopularPeripheral_SonyWF1000XM3_1_ModelId[] = "9C98DB";
const char kPopularPeripheral_SonyWF1000XM3_1_Name[] = "SonyWF1000XM3_1";
const char kPopularPeripheral_SonyWF1000XM3_2_ModelId[] = "3BC95C";
const char kPopularPeripheral_SonyWF1000XM3_2_Name[] = "SonyWF1000XM3_2";
const char kPopularPeripheral_SonyWF1000XM3_3_ModelId[] = "3AC95C";
const char kPopularPeripheral_SonyWF1000XM3_3_Name[] = "SonyWF1000XM3_3";
const char kPopularPeripheral_SonyWF1000XM3_4_ModelId[] = "0AC95C";
const char kPopularPeripheral_SonyWF1000XM3_4_Name[] = "SonyWF1000XM3_4";
const char kPopularPeripheral_SonyWF1000XM3_5_ModelId[] = "0DC95C";
const char kPopularPeripheral_SonyWF1000XM3_5_Name[] = "SonyWF1000XM3_5";
const char kPopularPeripheral_SonyWF1000XM3_6_ModelId[] = "0BC95C";
const char kPopularPeripheral_SonyWF1000XM3_6_Name[] = "SonyWF1000XM3_6";
const char kPopularPeripheral_SonyWF1000XM3_7_ModelId[] = "0CC95C";
const char kPopularPeripheral_SonyWF1000XM3_7_Name[] = "SonyWF1000XM3_7";

const char kPopularPeripheral_SonyWH1000XM3_ModelId[] = "0BC95C";
const char kPopularPeripheral_SonyWH1000XM3_Name[] = "SonyWH1000XM3";
const char kPopularPeripheral_SonyWH1000XM3_1_ModelId[] = "AC95C";
const char kPopularPeripheral_SonyWH1000XM3_1_Name[] = "SonyWH1000XM3_1";

const char kPopularPeripheral_SRSXB23_ModelId[] = "30D222";
const char kPopularPeripheral_SRSXB23_Name[] = "SRSXB23";
const char kPopularPeripheral_SRSXB23_1_ModelId[] = "438188";
const char kPopularPeripheral_SRSXB23_1_Name[] = "SRSXB23_1";
const char kPopularPeripheral_SRSXB23_2_ModelId[] = "4A9EF6";
const char kPopularPeripheral_SRSXB23_2_Name[] = "SRSXB23_2";
const char kPopularPeripheral_SRSXB23_3_ModelId[] = "6ABCC9";
const char kPopularPeripheral_SRSXB23_3_Name[] = "SRSXB23_3";
const char kPopularPeripheral_SRSXB23_4_ModelId[] = "3414EB";
const char kPopularPeripheral_SRSXB23_4_Name[] = "SRSXB23_4";

const char kPopularPeripheral_SRSXB33_ModelId[] = "20330C";
const char kPopularPeripheral_SRSXB33_Name[] = "SRSXB33";
const char kPopularPeripheral_SRSXB33_1_ModelId[] = "91DABC";
const char kPopularPeripheral_SRSXB33_1_Name[] = "SRSXB33_1";
const char kPopularPeripheral_SRSXB33_2_ModelId[] = "E5B91B";
const char kPopularPeripheral_SRSXB33_2_Name[] = "SRSXB33_2";
const char kPopularPeripheral_SRSXB33_3_ModelId[] = "5A0DDA";
const char kPopularPeripheral_SRSXB33_3_Name[] = "SRSXB33_3";

const char kPopularPeripheral_Other_Name[] = "Other";

const std::string GetFastPairTrackedModelId(const std::string& model_id) {
  if (model_id == kPopularPeripheral_JBLLIVE300TWS_ModelId)
    return kPopularPeripheral_JBLLIVE300TWS_Name;
  if (model_id == kPopularPeripheral_JBLLIVE300TWS_1_ModelId)
    return kPopularPeripheral_JBLLIVE300TWS_1_Name;
  if (model_id == kPopularPeripheral_JBLLIVE300TWS_2_ModelId)
    return kPopularPeripheral_JBLLIVE300TWS_2_Name;

  if (model_id == kPopularPeripheral_JBLLIVE400BT_ModelId)
    return kPopularPeripheral_JBLLIVE400BT_Name;
  if (model_id == kPopularPeripheral_JBLLIVE400BT_1_ModelId)
    return kPopularPeripheral_JBLLIVE400BT_1_Name;
  if (model_id == kPopularPeripheral_JBLLIVE400BT_2_ModelId)
    return kPopularPeripheral_JBLLIVE400BT_2_Name;
  if (model_id == kPopularPeripheral_JBLLIVE400BT_3_ModelId)
    return kPopularPeripheral_JBLLIVE400BT_3_Name;
  if (model_id == kPopularPeripheral_JBLLIVE400BT_4_ModelId)
    return kPopularPeripheral_JBLLIVE400BT_4_Name;

  if (model_id == kPopularPeripheral_JBLTUNE125TWS_ModelId)
    return kPopularPeripheral_JBLTUNE125TWS_Name;
  if (model_id == kPopularPeripheral_JBLTUNE125TWS_1_ModelId)
    return kPopularPeripheral_JBLTUNE125TWS_1_Name;
  if (model_id == kPopularPeripheral_JBLTUNE125TWS_2_ModelId)
    return kPopularPeripheral_JBLTUNE125TWS_2_Name;
  if (model_id == kPopularPeripheral_JBLTUNE125TWS_3_ModelId)
    return kPopularPeripheral_JBLTUNE125TWS_3_Name;
  if (model_id == kPopularPeripheral_JBLTUNE125TWS_4_ModelId)
    return kPopularPeripheral_JBLTUNE125TWS_4_Name;
  if (model_id == kPopularPeripheral_JBLTUNE125TWS_5_ModelId)
    return kPopularPeripheral_JBLTUNE125TWS_5_Name;

  if (model_id == kPopularPeripheral_JBLTUNE225TWS_ModelId)
    return kPopularPeripheral_JBLTUNE225TWS_Name;
  if (model_id == kPopularPeripheral_JBLTUNE225TWS_1_ModelId)
    return kPopularPeripheral_JBLTUNE225TWS_1_Name;
  if (model_id == kPopularPeripheral_JBLTUNE225TWS_2_ModelId)
    return kPopularPeripheral_JBLTUNE225TWS_2_Name;
  if (model_id == kPopularPeripheral_JBLTUNE225TWS_3_ModelId)
    return kPopularPeripheral_JBLTUNE225TWS_3_Name;
  if (model_id == kPopularPeripheral_JBLTUNE225TWS_4_ModelId)
    return kPopularPeripheral_JBLTUNE225TWS_4_Name;
  if (model_id == kPopularPeripheral_JBLTUNE225TWS_5_ModelId)
    return kPopularPeripheral_JBLTUNE225TWS_5_Name;

  if (model_id == kPopularPeripheral_OnePlusBudsZ_ModelId)
    return kPopularPeripheral_OnePlusBudsZ_Name;
  if (model_id == kPopularPeripheral_OnePlusBudsZ_1_ModelId)
    return kPopularPeripheral_OnePlusBudsZ_1_Name;
  if (model_id == kPopularPeripheral_OnePlusBudsZ_2_ModelId)
    return kPopularPeripheral_OnePlusBudsZ_2_Name;

  if (model_id == kPopularPeripheral_PixelBuds_ModelId)
    return kPopularPeripheral_PixelBuds_Name;

  if (model_id == kPopularPeripheral_PixelBudsASeries_ModelId)
    return kPopularPeripheral_PixelBudsASeries_Name;
  if (model_id == kPopularPeripheral_PixelBudsASeries_1_ModelId)
    return kPopularPeripheral_PixelBudsASeries_1_Name;
  if (model_id == kPopularPeripheral_PixelBudsASeries_2_ModelId)
    return kPopularPeripheral_PixelBudsASeries_2_Name;

  if (model_id == kPopularPeripheral_PixelBudsPro_ModelId)
    return kPopularPeripheral_PixelBudsPro_Name;
  if (model_id == kPopularPeripheral_PixelBudsPro_1_ModelId)
    return kPopularPeripheral_PixelBudsPro_1_Name;
  if (model_id == kPopularPeripheral_PixelBudsPro_2_ModelId)
    return kPopularPeripheral_PixelBudsPro_2_Name;
  if (model_id == kPopularPeripheral_PixelBudsPro_3_ModelId)
    return kPopularPeripheral_PixelBudsPro_3_Name;
  if (model_id == kPopularPeripheral_PixelBudsPro_4_ModelId)
    return kPopularPeripheral_PixelBudsPro_4_Name;

  if (model_id == kPopularPeripheral_RealMeBudsAir2_ModelId)
    return kPopularPeripheral_RealMeBudsAir2_Name;

  if (model_id == kPopularPeripheral_SonyWF1000XM3_ModelId)
    return kPopularPeripheral_SonyWF1000XM3_Name;
  if (model_id == kPopularPeripheral_SonyWF1000XM3_1_ModelId)
    return kPopularPeripheral_SonyWF1000XM3_1_Name;
  if (model_id == kPopularPeripheral_SonyWF1000XM3_2_ModelId)
    return kPopularPeripheral_SonyWF1000XM3_2_Name;
  if (model_id == kPopularPeripheral_SonyWF1000XM3_3_ModelId)
    return kPopularPeripheral_SonyWF1000XM3_3_Name;
  if (model_id == kPopularPeripheral_SonyWF1000XM3_4_ModelId)
    return kPopularPeripheral_SonyWF1000XM3_4_Name;
  if (model_id == kPopularPeripheral_SonyWF1000XM3_5_ModelId)
    return kPopularPeripheral_SonyWF1000XM3_5_Name;
  if (model_id == kPopularPeripheral_SonyWF1000XM3_6_ModelId)
    return kPopularPeripheral_SonyWF1000XM3_6_Name;
  if (model_id == kPopularPeripheral_SonyWF1000XM3_7_ModelId)
    return kPopularPeripheral_SonyWF1000XM3_7_Name;

  if (model_id == kPopularPeripheral_NothingEar1_ModelId)
    return kPopularPeripheral_NothingEar1_Name;
  if (model_id == kPopularPeripheral_NothingEar1_1_ModelId)
    return kPopularPeripheral_NothingEar1_1_Name;

  if (model_id == kPopularPeripheral_RealMeBudsAir2Neo_ModelId)
    return kPopularPeripheral_RealMeBudsAir2Neo_Name;

  if (model_id == kPopularPeripheral_JBLTUNE130NCTWS_ModelId)
    return kPopularPeripheral_JBLTUNE130NCTWS_Name;
  if (model_id == kPopularPeripheral_JBLTUNE130NCTWS_1_ModelId)
    return kPopularPeripheral_JBLTUNE130NCTWS_1_Name;
  if (model_id == kPopularPeripheral_JBLTUNE130NCTWS_2_ModelId)
    return kPopularPeripheral_JBLTUNE130NCTWS_2_Name;
  if (model_id == kPopularPeripheral_JBLTUNE130NCTWS_3_ModelId)
    return kPopularPeripheral_JBLTUNE130NCTWS_3_Name;

  if (model_id == kPopularPeripheral_SonyWH1000XM3_ModelId)
    return kPopularPeripheral_SonyWH1000XM3_Name;
  if (model_id == kPopularPeripheral_SonyWH1000XM3_1_ModelId)
    return kPopularPeripheral_SonyWH1000XM3_1_Name;

  if (model_id == kPopularPeripheral_JBLLIVEPROTWS_ModelId)
    return kPopularPeripheral_JBLLIVEPROTWS_Name;
  if (model_id == kPopularPeripheral_JBLLIVEPROTWS_1_ModelId)
    return kPopularPeripheral_JBLLIVEPROTWS_1_Name;
  if (model_id == kPopularPeripheral_JBLLIVEPROTWS_2_ModelId)
    return kPopularPeripheral_JBLLIVEPROTWS_2_Name;
  if (model_id == kPopularPeripheral_JBLLIVEPROTWS_3_ModelId)
    return kPopularPeripheral_JBLLIVEPROTWS_3_Name;
  if (model_id == kPopularPeripheral_JBLLIVEPROTWS_4_ModelId)
    return kPopularPeripheral_JBLLIVEPROTWS_4_Name;
  if (model_id == kPopularPeripheral_JBLLIVEPROTWS_5_ModelId)
    return kPopularPeripheral_JBLLIVEPROTWS_5_Name;
  if (model_id == kPopularPeripheral_JBLLIVEPROTWS_6_ModelId)
    return kPopularPeripheral_JBLLIVEPROTWS_6_Name;

  if (model_id == kPopularPeripheral_RealMeBudsAirPro_ModelId)
    return kPopularPeripheral_RealMeBudsAirPro_Name;
  if (model_id == kPopularPeripheral_RealMeBudsAirPro_1_ModelId)
    return kPopularPeripheral_RealMeBudsAirPro_1_Name;
  if (model_id == kPopularPeripheral_RealMeBudsAirPro_2_ModelId)
    return kPopularPeripheral_RealMeBudsAirPro_2_Name;

  if (model_id == kPopularPeripheral_SRSXB33_ModelId)
    return kPopularPeripheral_SRSXB33_Name;
  if (model_id == kPopularPeripheral_SRSXB33_1_ModelId)
    return kPopularPeripheral_SRSXB33_1_Name;
  if (model_id == kPopularPeripheral_SRSXB33_2_ModelId)
    return kPopularPeripheral_SRSXB33_2_Name;
  if (model_id == kPopularPeripheral_SRSXB33_3_ModelId)
    return kPopularPeripheral_SRSXB33_3_Name;

  if (model_id == kPopularPeripheral_SRSXB23_ModelId)
    return kPopularPeripheral_SRSXB23_Name;
  if (model_id == kPopularPeripheral_SRSXB23_1_ModelId)
    return kPopularPeripheral_SRSXB23_1_Name;
  if (model_id == kPopularPeripheral_SRSXB23_2_ModelId)
    return kPopularPeripheral_SRSXB23_2_Name;
  if (model_id == kPopularPeripheral_SRSXB23_3_ModelId)
    return kPopularPeripheral_SRSXB23_3_Name;
  if (model_id == kPopularPeripheral_SRSXB23_4_ModelId)
    return kPopularPeripheral_SRSXB23_4_Name;

  return kPopularPeripheral_Other_Name;
}

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused. This enum should be kept in sync
// with the BluetoothConnectToServiceError enum in
// src/tools/metrics/histograms/enums.xml.
enum class ConnectToServiceError {
  kUnknownError = 0,
  kAcceptFailed = 1,
  kInvalidUUID = 2,
  kSocketNotListening = 3,
  kMaxValue = kSocketNotListening,
};

ConnectToServiceError GetConnectToServiceError(const std::string& error) {
  if (error == kAcceptFailedString)
    return ConnectToServiceError::kAcceptFailed;

  if (error == kInvalidUUIDString)
    return ConnectToServiceError::kInvalidUUID;

  if (error == kSocketNotListeningString)
    return ConnectToServiceError::kSocketNotListening;

  DCHECK(error != kSocketNotListeningString && error != kInvalidUUIDString &&
         error != kAcceptFailedString);
  return ConnectToServiceError::kUnknownError;
}

const char kEngagementFlowInitialMetric[] =
    "Bluetooth.ChromeOS.FastPair.EngagementFunnel.Steps.InitialPairingProtocol";
const char kEngagementFlowSubsequentMetric[] =
    "Bluetooth.ChromeOS.FastPair.EngagementFunnel.Steps."
    "SubsequentPairingProtocol";
const char kTotalUxPairTimeInitialMetric[] =
    "Bluetooth.ChromeOS.FastPair.TotalUxPairTime.InitialPairingProtocol2";
const char kTotalUxPairTimeSubsequentMetric[] =
    "Bluetooth.ChromeOS.FastPair.TotalUxPairTime.SubsequentPairingProtocol2";
const char kRetroactiveEngagementFlowMetric[] =
    "Bluetooth.ChromeOS.FastPair.RetroactiveEngagementFunnel.Steps";
const char kPairingMethodMetric[] = "Bluetooth.ChromeOS.FastPair.PairingMethod";
const char kRetroactivePairingResultMetric[] =
    "Bluetooth.ChromeOS.FastPair.RetroactivePairing.Result";
const char kTotalGattConnectionTimeMetric[] =
    "Bluetooth.ChromeOS.FastPair.TotalGattConnectionTime";
const char kGattConnectionResult[] =
    "Bluetooth.ChromeOS.FastPair.GattConnection.Result";
const char kGattConnectionErrorMetric[] =
    "Bluetooth.ChromeOS.FastPair.GattConnection.ErrorReason";
const char kFastPairPairFailureInitialMetric[] =
    "Bluetooth.ChromeOS.FastPair.PairFailure.InitialPairingProtocol";
const char kFastPairPairFailureSubsequentMetric[] =
    "Bluetooth.ChromeOS.FastPair.PairFailure.SubsequentPairingProtocol";
const char kFastPairPairFailureRetroactiveMetric[] =
    "Bluetooth.ChromeOS.FastPair.PairFailure.RetroactivePairingProtocol";
const char kFastPairPairResultInitialMetric[] =
    "Bluetooth.ChromeOS.FastPair.Pairing.Result.InitialPairingProtocol";
const char kFastPairPairResultSubsequentMetric[] =
    "Bluetooth.ChromeOS.FastPair.Pairing.Result.SubsequentPairingProtocol";
const char kFastPairPairResultRetroactiveMetric[] =
    "Bluetooth.ChromeOS.FastPair.Pairing.Result.RetroactivePairingProtocol";
const char kFastPairAccountKeyWriteResultInitialMetric[] =
    "Bluetooth.ChromeOS.FastPair.AccountKey.Write.Result."
    "InitialPairingProtocol";
const char kFastPairAccountKeyWriteResultSubsequentMetric[] =
    "Bluetooth.ChromeOS.FastPair.AccountKey.Write.Result."
    "SubsequentPairingProtocol";
const char kFastPairAccountKeyWriteResultRetroactiveMetric[] =
    "Bluetooth.ChromeOS.FastPair.AccountKey.Write.Result."
    "RetroactivePairingProtocol";
const char kFastPairAccountKeyWriteFailureInitialMetric[] =
    "Bluetooth.ChromeOS.FastPair.AccountKey.Failure.InitialPairingProtocol";
const char kFastPairAccountKeyWriteFailureRetroactiveMetric[] =
    "Bluetooth.ChromeOS.FastPair.AccountKey.Failure.RetroactivePairingProtocol";
const char kKeyGenerationResultMetric[] =
    "Bluetooth.ChromeOS.FastPair.KeyGenerationResult";
const char kDataEncryptorCreateResultMetric[] =
    "Bluetooth.ChromeOS.FastPair.FastPairDataEncryptor.CreateResult";
const char kWriteKeyBasedCharacteristicResult[] =
    "Bluetooth.ChromeOS.FastPair.KeyBasedPairing.Write.Result";
const char kWriteKeyBasedCharacteristicPairFailure[] =
    "Bluetooth.ChromeOS.FastPair.KeyBasedPairing.Write.PairFailure";
const char kWriteKeyBasedCharacteristicGattError[] =
    "Bluetooth.ChromeOS.FastPair.KeyBasedPairing.Write.GattErrorReason";
const char kNotifyKeyBasedCharacteristicTime[] =
    "Bluetooth.ChromeOS.FastPair.KeyBasedPairing.NotifyTime";
const char kKeyBasedCharacteristicDecryptTime[] =
    "Bluetooth.ChromeOS.FastPair.KeyBasedPairing.DecryptTime";
const char kKeyBasedCharacteristicDecryptResult[] =
    "Bluetooth.ChromeOS.FastPair.KeyBasedPairing.DecryptResult";
const char kWritePasskeyCharacteristicResult[] =
    "Bluetooth.ChromeOS.FastPair.Passkey.Write.Result";
const char kWritePasskeyCharacteristicPairFailure[] =
    "Bluetooth.ChromeOS.FastPair.Passkey.Write.PairFailure";
const char kWritePasskeyCharacteristicGattError[] =
    "Bluetooth.ChromeOS.FastPair.Passkey.Write.GattErrorReason";
const char kNotifyPasskeyCharacteristicTime[] =
    "Bluetooth.ChromeOS.FastPair.Passkey.NotifyTime";
const char kPasskeyCharacteristicDecryptTime[] =
    "Bluetooth.ChromeOS.FastPair.Passkey.Decrypt.Time";
const char kPasskeyCharacteristicDecryptResult[] =
    "Bluetooth.ChromeOS.FastPair.Passkey.Decrypt.Result";
const char kWriteAccountKeyCharacteristicResult[] =
    "Bluetooth.ChromeOS.FastPair.AccountKey.Write.Result";
const char kWriteAccountKeyCharacteristicGattError[] =
    "Bluetooth.ChromeOS.FastPair.AccountKey.Write.GattErrorReason";
const char kWriteAccountKeyTime[] =
    "Bluetooth.ChromeOS.FastPair.AccountKey.Write.TotalTime";
const char kTotalDataEncryptorCreateTime[] =
    "Bluetooth.ChromeOS.FastPair.FastPairDataEncryptor.CreateTime";
const char kMessageStreamReceiveResult[] =
    "Bluetooth.ChromeOS.FastPair.MessageStream.Receive.Result";
const char kMessageStreamReceiveError[] =
    "Bluetooth.ChromeOS.FastPair.MessageStream.Receive.ErrorReason";
const char kMessageStreamConnectToServiceError[] =
    "Bluetooth.ChromeOS.FastPair.MessageStream.ConnectToService.ErrorReason";
const char kMessageStreamConnectToServiceResult[] =
    "Bluetooth.ChromeOS.FastPair.MessageStream.ConnectToService.Result";
const char kMessageStreamConnectToServiceTime[] =
    "Bluetooth.ChromeOS.FastPair.MessageStream.ConnectToService."
    "TotalConnectTime";
const char kDeviceMetadataFetchResult[] =
    "Bluetooth.ChromeOS.FastPair.DeviceMetadataFetcher.Result";
const char kDeviceMetadataFetchNetError[] =
    "Bluetooth.ChromeOS.FastPair.DeviceMetadataFetcher.Get.NetError";
const char kDeviceMetadataFetchHttpResponseError[] =
    "Bluetooth.ChromeOS.FastPair.DeviceMetadataFetcher.Get.HttpResponseError";
const char kFootprintsFetcherDeleteResult[] =
    "Bluetooth.ChromeOS.FastPair.FootprintsFetcher.Delete.Result";
const char kFootprintsFetcherDeleteNetError[] =
    "Bluetooth.ChromeOS.FastPair.FootprintsFetcher.Delete.NetError";
const char kFootprintsFetcherDeleteHttpResponseError[] =
    "Bluetooth.ChromeOS.FastPair.FootprintsFetcher.Delete.HttpResponseError";
const char kFootprintsFetcherPostResult[] =
    "Bluetooth.ChromeOS.FastPair.FootprintsFetcher.Post.Result";
const char kFootprintsFetcherPostNetError[] =
    "Bluetooth.ChromeOS.FastPair.FootprintsFetcher.Post.NetError";
const char kFootprintsFetcherPostHttpResponseError[] =
    "Bluetooth.ChromeOS.FastPair.FootprintsFetcher.Post.HttpResponseError";
const char kFootprintsFetcherGetResult[] =
    "Bluetooth.ChromeOS.FastPair.FootprintsFetcher.Get.Result";
const char kFootprintsFetcherGetNetError[] =
    "Bluetooth.ChromeOS.FastPair.FootprintsFetcher.Get.NetError";
const char kFootprintsFetcherGetHttpResponseError[] =
    "Bluetooth.ChromeOS.FastPair.FootprintsFetcher.Get.HttpResponseError";
const char kFastPairRepositoryCacheResult[] =
    "Bluetooth.ChromeOS.FastPair.FastPairRepository.Cache.Result";
const char kHandshakeResult[] = "Bluetooth.ChromeOS.FastPair.Handshake.Result";
const char kFastPairHandshakeStepInitial[] =
    "Bluetooth.ChromeOS.FastPair.Handshake.Steps.InitialPairingProtocol";
const char kFastPairHandshakeStepSubsequent[] =
    "Bluetooth.ChromeOS.FastPair.Handshake.Steps.SubsequentPairingProtocol";
const char kFastPairHandshakeStepRetroactive[] =
    "Bluetooth.ChromeOS.FastPair.Handshake.Steps.RetroactivePairingProtocol";
const char kHandshakeFailureReason[] =
    "Bluetooth.ChromeOS.FastPair.Handshake.FailureReason";
const char kBleScanSessionResult[] =
    "Bluetooth.ChromeOS.FastPair.Scanner.StartSession.Result";
const char kBleScanFilterResult[] =
    "Bluetooth.ChromeOS.FastPair.CreateScanFilter.Result";
const char kFastPairVersion[] =
    "Bluetooth.ChromeOS.FastPair.Discovered.Version";
const char kNavigateToSettings[] =
    "Bluetooth.ChromeOS.FastPair.NavigateToSettings.Result";
const char kConnectDeviceResult[] =
    "Bluetooth.ChromeOS.FastPair.ConnectDevice.Result";
const char kPairDeviceResult[] =
    "Bluetooth.ChromeOS.FastPair.PairDevice.Result";
const char kPairDeviceErrorReason[] =
    "Bluetooth.ChromeOS.FastPair.PairDevice.ErrorReason";
const char kConfirmPasskeyAskTime[] =
    "Bluetooth.ChromeOS.FastPair.RequestPasskey.Latency";
const char kConfirmPasskeyConfirmTime[] =
    "Bluetooth.ChromeOS.FastPair.ConfirmPasskey.Latency";
const char kFastPairRetryCount[] =
    "Bluetooth.ChromeOS.FastPair.PairRetry.Count";
const char kSavedDeviceRemoveResult[] =
    "Bluetooth.ChromeOS.FastPair.SavedDevices.Remove.Result";
const char kSavedDeviceUpdateOptInStatusInitialResult[] =
    "Bluetooth.ChromeOS.FastPair.SavedDevices.UpdateOptInStatus.Result."
    "InitialPairingProtocol";
const char kSavedDeviceUpdateOptInStatusRetroactiveResult[] =
    "Bluetooth.ChromeOS.FastPair.SavedDevices.UpdateOptInStatus.Result."
    "RetroactivePairingProtocol";
const char kSavedDeviceUpdateOptInStatusSubsequentResult[] =
    "Bluetooth.ChromeOS.FastPair.SavedDevices.UpdateOptInStatus.Result."
    "SubsequentPairingProtocol";
const char kSavedDeviceGetDevicesResult[] =
    "Bluetooth.ChromeOS.FastPair.SavedDevices.GetSavedDevices.Result";
const char kSavedDevicesTotalUxLoadTime[] =
    "Bluetooth.ChromeOS.FastPair.SavedDevices.TotalUxLoadTime";
const char kSavedDevicesCount[] =
    "Bluetooth.ChromeOS.FastPair.SavedDevices.DeviceCount";

const std::string GetEngagementFlowInitialModelIdMetric(
    const ash::quick_pair::Device& device) {
  return std::string(kEngagementFlowInitialMetric) + "." +
         GetFastPairTrackedModelId(device.metadata_id);
}

const std::string GetEngagementFlowSubsequentModelIdMetric(
    const ash::quick_pair::Device& device) {
  return std::string(kEngagementFlowSubsequentMetric) + "." +
         GetFastPairTrackedModelId(device.metadata_id);
}

const std::string GetRetroactiveEngagementFlowModelIdMetric(
    const ash::quick_pair::Device& device) {
  return std::string(kRetroactiveEngagementFlowMetric) + "." +
         GetFastPairTrackedModelId(device.metadata_id);
}

// The retroactive engagement flow doesn't record retroactive successes
// properly due to b/240581398, so we use the account key write metric
// to record metrics split by model ID.
const std::string GetAccountKeyWriteResultRetroactiveModelIdMetric(
    const ash::quick_pair::Device& device) {
  return std::string(kFastPairAccountKeyWriteResultRetroactiveMetric) + "." +
         GetFastPairTrackedModelId(device.metadata_id);
}

}  // namespace

namespace ash {
namespace quick_pair {

void AttemptRecordingFastPairEngagementFlow(const Device& device,
                                            FastPairEngagementFlowEvent event) {
  switch (device.protocol) {
    case Protocol::kFastPairInitial:
      base::UmaHistogramSparse(kEngagementFlowInitialMetric,
                               static_cast<int>(event));
      // Also record engagement flow metrics split per tracked model ID.
      base::UmaHistogramSparse(GetEngagementFlowInitialModelIdMetric(device),
                               static_cast<int>(event));
      break;
    case Protocol::kFastPairRetroactive:
      break;
    case Protocol::kFastPairSubsequent:
      base::UmaHistogramSparse(kEngagementFlowSubsequentMetric,
                               static_cast<int>(event));
      // Also record engagement flow metrics split per tracked model ID.
      base::UmaHistogramSparse(GetEngagementFlowSubsequentModelIdMetric(device),
                               static_cast<int>(event));
      break;
  }
}

void AttemptRecordingTotalUxPairTime(const Device& device,
                                     base::TimeDelta total_pair_time) {
  switch (device.protocol) {
    case Protocol::kFastPairInitial:
      base::UmaHistogramCustomTimes(kTotalUxPairTimeInitialMetric,
                                    total_pair_time, base::Milliseconds(1),
                                    base::Seconds(25), 50);
      break;
    case Protocol::kFastPairRetroactive:
      break;
    case Protocol::kFastPairSubsequent:
      base::UmaHistogramCustomTimes(kTotalUxPairTimeSubsequentMetric,
                                    total_pair_time, base::Milliseconds(1),
                                    base::Seconds(25), 50);
      break;
  }
}

void AttemptRecordingFastPairRetroactiveEngagementFlow(
    const Device& device,
    FastPairRetroactiveEngagementFlowEvent event) {
  switch (device.protocol) {
    case Protocol::kFastPairInitial:
    case Protocol::kFastPairSubsequent:
      break;
    case Protocol::kFastPairRetroactive:
      base::UmaHistogramSparse(kRetroactiveEngagementFlowMetric,
                               static_cast<int>(event));
      // Also record engagement flow metrics split per tracked model ID.
      base::UmaHistogramSparse(
          GetRetroactiveEngagementFlowModelIdMetric(device),
          static_cast<int>(event));
      break;
  }
}

void RecordPairingMethod(PairingMethod method) {
  base::UmaHistogramEnumeration(kPairingMethodMetric, method);
}

void RecordRetroactivePairingResult(bool success) {
  base::UmaHistogramBoolean(kRetroactivePairingResultMetric, success);
}

void RecordTotalGattConnectionTime(base::TimeDelta total_gatt_connection_time) {
  base::UmaHistogramTimes(kTotalGattConnectionTimeMetric,
                          total_gatt_connection_time);
}

void RecordGattConnectionResult(bool success) {
  base::UmaHistogramBoolean(kGattConnectionResult, success);
}

void RecordGattConnectionErrorCode(
    device::BluetoothDevice::ConnectErrorCode error_code) {
  base::UmaHistogramEnumeration(
      kGattConnectionErrorMetric, error_code,
      device::BluetoothDevice::ConnectErrorCode::NUM_CONNECT_ERROR_CODES);
}

void RecordPairingResult(const Device& device, bool success) {
  switch (device.protocol) {
    case Protocol::kFastPairInitial:
      base::UmaHistogramBoolean(kFastPairPairResultInitialMetric, success);
      break;
    case Protocol::kFastPairRetroactive:
      base::UmaHistogramBoolean(kFastPairPairResultRetroactiveMetric, success);
      break;
    case Protocol::kFastPairSubsequent:
      base::UmaHistogramBoolean(kFastPairPairResultSubsequentMetric, success);
      break;
  }
}

void RecordPairingFailureReason(const Device& device, PairFailure failure) {
  switch (device.protocol) {
    case Protocol::kFastPairInitial:
      base::UmaHistogramEnumeration(kFastPairPairFailureInitialMetric, failure);
      break;
    case Protocol::kFastPairRetroactive:
      base::UmaHistogramEnumeration(kFastPairPairFailureRetroactiveMetric,
                                    failure);
      break;
    case Protocol::kFastPairSubsequent:
      base::UmaHistogramEnumeration(kFastPairPairFailureSubsequentMetric,
                                    failure);
      break;
  }
}

void RecordAccountKeyFailureReason(const Device& device,
                                   AccountKeyFailure failure) {
  switch (device.protocol) {
    case Protocol::kFastPairInitial:
      base::UmaHistogramEnumeration(
          kFastPairAccountKeyWriteFailureInitialMetric, failure);
      break;
    case Protocol::kFastPairRetroactive:
      base::UmaHistogramEnumeration(
          kFastPairAccountKeyWriteFailureRetroactiveMetric, failure);
      break;
    case Protocol::kFastPairSubsequent:
      break;
  }
}

void RecordAccountKeyResult(const Device& device, bool success) {
  switch (device.protocol) {
    case Protocol::kFastPairInitial:
      base::UmaHistogramBoolean(kFastPairAccountKeyWriteResultInitialMetric,
                                success);
      break;
    case Protocol::kFastPairRetroactive:
      base::UmaHistogramBoolean(kFastPairAccountKeyWriteResultRetroactiveMetric,
                                success);
      // Also record engagement flow metrics split per tracked model ID.
      base::UmaHistogramBoolean(
          GetAccountKeyWriteResultRetroactiveModelIdMetric(device), success);
      break;
    case Protocol::kFastPairSubsequent:
      base::UmaHistogramBoolean(kFastPairAccountKeyWriteResultSubsequentMetric,
                                success);
      break;
  }
}

void RecordKeyPairGenerationResult(bool success) {
  base::UmaHistogramBoolean(kKeyGenerationResultMetric, success);
}

void RecordDataEncryptorCreateResult(bool success) {
  base::UmaHistogramBoolean(kDataEncryptorCreateResultMetric, success);
}

void RecordWriteKeyBasedCharacteristicResult(bool success) {
  base::UmaHistogramBoolean(kWriteKeyBasedCharacteristicResult, success);
}

void RecordWriteKeyBasedCharacteristicPairFailure(PairFailure failure) {
  base::UmaHistogramEnumeration(kWriteKeyBasedCharacteristicPairFailure,
                                failure);
}

void RecordWriteRequestGattError(
    device::BluetoothGattService::GattErrorCode error) {
  base::UmaHistogramEnumeration(kWriteKeyBasedCharacteristicGattError, error);
}

void RecordNotifyKeyBasedCharacteristicTime(base::TimeDelta total_notify_time) {
  base::UmaHistogramTimes(kNotifyKeyBasedCharacteristicTime, total_notify_time);
}

void RecordKeyBasedCharacteristicDecryptTime(base::TimeDelta decrypt_time) {
  base::UmaHistogramTimes(kKeyBasedCharacteristicDecryptTime, decrypt_time);
}

void RecordKeyBasedCharacteristicDecryptResult(bool success) {
  base::UmaHistogramBoolean(kKeyBasedCharacteristicDecryptResult, success);
}

void RecordWritePasskeyCharacteristicResult(bool success) {
  base::UmaHistogramBoolean(kWritePasskeyCharacteristicResult, success);
}

void RecordWritePasskeyCharacteristicPairFailure(PairFailure failure) {
  base::UmaHistogramEnumeration(kWritePasskeyCharacteristicPairFailure,
                                failure);
}

void RecordWritePasskeyGattError(
    device::BluetoothGattService::GattErrorCode error) {
  base::UmaHistogramEnumeration(kWritePasskeyCharacteristicGattError, error);
}

void RecordNotifyPasskeyCharacteristicTime(base::TimeDelta total_notify_time) {
  base::UmaHistogramTimes(kNotifyPasskeyCharacteristicTime, total_notify_time);
}

void RecordPasskeyCharacteristicDecryptTime(base::TimeDelta decrypt_time) {
  base::UmaHistogramTimes(kPasskeyCharacteristicDecryptTime, decrypt_time);
}

void RecordPasskeyCharacteristicDecryptResult(bool success) {
  base::UmaHistogramBoolean(kPasskeyCharacteristicDecryptResult, success);
}

void RecordWriteAccountKeyCharacteristicResult(bool success) {
  base::UmaHistogramBoolean(kWriteAccountKeyCharacteristicResult, success);
}

void RecordWriteAccountKeyGattError(
    device::BluetoothGattService::GattErrorCode error) {
  base::UmaHistogramEnumeration(kWriteAccountKeyCharacteristicGattError, error);
}

void RecordWriteAccountKeyTime(base::TimeDelta write_time) {
  base::UmaHistogramTimes(kWriteAccountKeyTime, write_time);
}

void RecordTotalDataEncryptorCreateTime(base::TimeDelta total_create_time) {
  base::UmaHistogramTimes(kTotalDataEncryptorCreateTime, total_create_time);
}

void RecordMessageStreamReceiveResult(bool success) {
  base::UmaHistogramBoolean(kMessageStreamReceiveResult, success);
}

void RecordMessageStreamReceiveError(
    device::BluetoothSocket::ErrorReason error) {
  base::UmaHistogramEnumeration(kMessageStreamReceiveError, error);
}

void RecordMessageStreamConnectToServiceResult(bool success) {
  base::UmaHistogramBoolean(kMessageStreamConnectToServiceResult, success);
}

void RecordMessageStreamConnectToServiceError(const std::string& error) {
  base::UmaHistogramEnumeration(kMessageStreamConnectToServiceError,
                                GetConnectToServiceError(error));
}

void RecordMessageStreamConnectToServiceTime(
    base::TimeDelta total_connect_time) {
  base::UmaHistogramTimes(kMessageStreamConnectToServiceTime,
                          total_connect_time);
}

void RecordDeviceMetadataFetchResult(const FastPairHttpResult& result) {
  base::UmaHistogramBoolean(kDeviceMetadataFetchResult, result.IsSuccess());

  if (result.net_error()) {
    base::UmaHistogramSparse(kDeviceMetadataFetchNetError,
                             -*result.net_error());
  }

  if (result.http_response_error()) {
    base::UmaHistogramSparse(kDeviceMetadataFetchHttpResponseError,
                             *result.http_response_error());
  }
}

void RecordFootprintsFetcherDeleteResult(const FastPairHttpResult& result) {
  base::UmaHistogramBoolean(kFootprintsFetcherDeleteResult, result.IsSuccess());

  if (result.net_error()) {
    base::UmaHistogramSparse(kFootprintsFetcherDeleteNetError,
                             -*result.net_error());
  }

  if (result.http_response_error()) {
    base::UmaHistogramSparse(kFootprintsFetcherDeleteHttpResponseError,
                             *result.http_response_error());
  }
}

void RecordFootprintsFetcherPostResult(const FastPairHttpResult& result) {
  base::UmaHistogramBoolean(kFootprintsFetcherPostResult, result.IsSuccess());

  if (result.net_error()) {
    base::UmaHistogramSparse(kFootprintsFetcherPostNetError,
                             -*result.net_error());
  }

  if (result.http_response_error()) {
    base::UmaHistogramSparse(kFootprintsFetcherPostHttpResponseError,
                             *result.http_response_error());
  }
}

void RecordFootprintsFetcherGetResult(const FastPairHttpResult& result) {
  base::UmaHistogramBoolean(kFootprintsFetcherGetResult, result.IsSuccess());

  if (result.net_error()) {
    base::UmaHistogramSparse(kFootprintsFetcherGetNetError,
                             -*result.net_error());
  }

  if (result.http_response_error()) {
    base::UmaHistogramSparse(kFootprintsFetcherGetHttpResponseError,
                             *result.http_response_error());
  }
}

void RecordFastPairRepositoryCacheResult(bool success) {
  base::UmaHistogramBoolean(kFastPairRepositoryCacheResult, success);
}

void RecordHandshakeResult(bool success) {
  base::UmaHistogramBoolean(kHandshakeResult, success);
}

void RecordHandshakeFailureReason(HandshakeFailureReason failure_reason) {
  base::UmaHistogramEnumeration(kHandshakeFailureReason, failure_reason);
}

void RecordHandshakeStep(FastPairHandshakeSteps handshake_step,
                         const Device& device) {
  switch (device.protocol) {
    case Protocol::kFastPairInitial:
      base::UmaHistogramEnumeration(kFastPairHandshakeStepInitial,
                                    handshake_step);
      break;
    case Protocol::kFastPairRetroactive:
      base::UmaHistogramEnumeration(kFastPairHandshakeStepRetroactive,
                                    handshake_step);
      break;
    case Protocol::kFastPairSubsequent:
      base::UmaHistogramEnumeration(kFastPairHandshakeStepSubsequent,
                                    handshake_step);
      break;
  }
}

void RecordBluetoothLowEnergyScannerStartSessionResult(bool success) {
  base::UmaHistogramBoolean(kBleScanSessionResult, success);
}

void RecordBluetoothLowEnergyScanFilterResult(bool success) {
  base::UmaHistogramBoolean(kBleScanFilterResult, success);
}

void RecordFastPairDiscoveredVersion(FastPairVersion version) {
  base::UmaHistogramEnumeration(kFastPairVersion, version);
}

void RecordNavigateToSettingsResult(bool success) {
  base::UmaHistogramBoolean(kNavigateToSettings, success);
}

void RecordConnectDeviceResult(bool success) {
  base::UmaHistogramBoolean(kConnectDeviceResult, success);
}

void RecordPairDeviceResult(bool success) {
  base::UmaHistogramBoolean(kPairDeviceResult, success);
}

void RecordPairDeviceErrorReason(
    device::BluetoothDevice::ConnectErrorCode error_code) {
  base::UmaHistogramEnumeration(
      kPairDeviceErrorReason, error_code,
      device::BluetoothDevice::NUM_CONNECT_ERROR_CODES);
}

void RecordConfirmPasskeyConfirmTime(base::TimeDelta total_confirm_time) {
  base::UmaHistogramTimes(kConfirmPasskeyConfirmTime, total_confirm_time);
}

void RecordConfirmPasskeyAskTime(base::TimeDelta total_ask_time) {
  base::UmaHistogramTimes(kConfirmPasskeyAskTime, total_ask_time);
}

void RecordPairFailureRetry(int num_retries) {
  base::UmaHistogramExactLinear(kFastPairRetryCount, num_retries,
                                /*exclusive_max=*/10);
}

void RecordSavedDevicesRemoveResult(bool success) {
  base::UmaHistogramBoolean(kSavedDeviceRemoveResult, success);
}

void RecordSavedDevicesUpdatedOptInStatusResult(const Device& device,
                                                bool success) {
  switch (device.protocol) {
    case Protocol::kFastPairInitial:
      base::UmaHistogramBoolean(kSavedDeviceUpdateOptInStatusInitialResult,
                                success);
      break;
    case Protocol::kFastPairRetroactive:
      base::UmaHistogramBoolean(kSavedDeviceUpdateOptInStatusRetroactiveResult,
                                success);
      break;
    case Protocol::kFastPairSubsequent:
      base::UmaHistogramBoolean(kSavedDeviceUpdateOptInStatusSubsequentResult,
                                success);
      break;
  }
}

void RecordGetSavedDevicesResult(bool success) {
  base::UmaHistogramBoolean(kSavedDeviceGetDevicesResult, success);
}

void RecordSavedDevicesTotalUxLoadTime(base::TimeDelta total_load_time) {
  base::UmaHistogramCustomTimes(kSavedDevicesTotalUxLoadTime, total_load_time,
                                base::Milliseconds(1), base::Seconds(25), 50);
}

void RecordSavedDevicesCount(int num_devices) {
  base::UmaHistogramCounts100(kSavedDevicesCount, num_devices);
}

}  // namespace quick_pair
}  // namespace ash
