// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/ash/services/ime/public/cpp/rulebased/def/kn_phone.h"

#include <iterator>

namespace kn_phone {

const char* kId = "kn_phone";
bool kIs102 = false;
const char* kTransforms[] = {"0",
                             "\u0ce6",
                             "1",
                             "\u0ce7",
                             "2",
                             "\u0ce8",
                             "3",
                             "\u0ce9",
                             "4",
                             "\u0cea",
                             "5",
                             "\u0ceb",
                             "6",
                             "\u0cec",
                             "7",
                             "\u0ced",
                             "8",
                             "\u0cee",
                             "9",
                             "\u0cef",
                             "([\u0c95-\u0cb9\u0cde])\u001d?u",
                             "\\1\u0ccc",
                             "\u0cca\u001d?o",
                             "\u0ccb",
                             "\u0ccd\u001d?O",
                             "\u0ccb",
                             "\u0ccd\u001d?o",
                             "\u0cca",
                             "([\u0c95-\u0cb9\u0cde])\u001d?i",
                             "\\1\u0cc8",
                             "\u0cc6\u001d?e",
                             "\u0cc7",
                             "\u0ccd\u001d?E",
                             "\u0cc7",
                             "\u0ccd\u001d?e",
                             "\u0cc6",
                             "\u0ccd\u0cb3\u0ccd\u0cb3\u0ccd\u001d?I",
                             "\u0ce3",
                             "\u0ccd\u0cb3\u0ccd\u001d?\\^I",
                             "\u0ce3",
                             "\u0ccd\u0cb3\u0ccd\u0cb3\u0ccd\u001d?i",
                             "\u0ce2",
                             "\u0ccd\u0cb3\u0ccd\u001d?\\^i",
                             "\u0ce2",
                             "\u0cc3\u001d?I",
                             "\u0cc4",
                             "\u0cc3\u001d?\\^I",
                             "\u0cc4",
                             "\u0cc3\u001d?R",
                             "\u0cc4",
                             "\u0cc3\u001d?\\^i",
                             "\u0cc3",
                             "\u0cc4\u001d?i",
                             "\u0cc3",
                             "\u0ccd\u001d?R",
                             "\u0cc3",
                             "\u0cc1\u001d?u",
                             "\u0cc2",
                             "\u0ccd\u001d?U",
                             "\u0cc2",
                             "\u0ccd\u001d?u",
                             "\u0cc1",
                             "\u0cbf\u001d?i",
                             "\u0cc0",
                             "\u0ccd\u001d?I",
                             "\u0cc0",
                             "\u0ccd\u001d?i",
                             "\u0cbf",
                             "([\u0c95-\u0cb9\u0cde])\u001d?a",
                             "\\1\u0cbe",
                             "\u0ccd\u001d?A",
                             "\u0cbe",
                             "\u0ccd\u001d?a",
                             "",
                             "\\.a",
                             "\u0cbd",
                             "\\.m",
                             "\u0c82",
                             "\\.z",
                             "\u0cbc",
                             "\\.N",
                             "\u0901",
                             "\u0ccd\u001d?\\.h",
                             "\u0ccd\u200c",
                             "\\.h",
                             "\u0ccd\u200c",
                             "M",
                             "\u0c82",
                             "H",
                             "\u0c83",
                             "\u0c95\u0ccd\u001d?H",
                             "\u0cf1",
                             "\u0caa\u0ccd\u001d?H",
                             "\u0cf2",
                             "\u0c85\u001d?u",
                             "\u0c94",
                             "\u0c92\u001d?o",
                             "\u0c93",
                             "O",
                             "\u0c93",
                             "o",
                             "\u0c92",
                             "\u0c85\u001d?i",
                             "\u0c90",
                             "\u0c8e\u001d?e",
                             "\u0c8f",
                             "E",
                             "\u0c8f",
                             "e",
                             "\u0c8e",
                             "\u0cb3\u0ccd\u0cb3\u0ccd\u001d?I",
                             "\u0ce1",
                             "\u0cb3\u0ccd\u001d?\\^I",
                             "\u0ce1",
                             "\u0cb3\u0ccd\u0cb3\u0ccd\u001d?i",
                             "\u0c8c",
                             "\u0cb3\u0ccd\u001d?^i",
                             "\u0c8c",
                             "\u0ce0\u001d?I",
                             "\u0ce0",
                             "\u0ce0\u001d?^I",
                             "\u0ce0",
                             "\u0c8b\u001d?R",
                             "\u0ce0",
                             "R",
                             "\u0c8b",
                             "\u0c89\u001d?u",
                             "\u0c8a",
                             "U",
                             "\u0c8a",
                             "u",
                             "\u0c89",
                             "\u0c87\u001d?i",
                             "\u0c88",
                             "I",
                             "\u0c88",
                             "i",
                             "\u0c87",
                             "A",
                             "\u0c86",
                             "\u0c85\u001d?a",
                             "\u0c86",
                             "a",
                             "\u0c85",
                             "\u0c95\u0ccd\u0cb7\u0ccd\u001d?h",
                             "\u0c95\u0ccd\u0cb7\u0ccd",
                             "\u0c97\u0ccd\u001d?Y",
                             "\u0c9c\u0ccd\u0c9e\u0ccd",
                             "\u0c9c\u0ccd\u001d?n",
                             "\u0c9c\u0ccd\u0c9e\u0ccd",
                             "\u0c95\u0ccd\u001d?S",
                             "\u0c95\u0ccd\u0cb7\u0ccd",
                             "\u0c95\u0ccd\u0cb8\u0ccd\u001d?h",
                             "\u0c95\u0ccd\u0cb7\u0ccd",
                             "x",
                             "\u0c95\u0ccd\u0cb7\u0ccd",
                             "h",
                             "\u0cb9\u0ccd",
                             "\u0cb7\u0ccd\u001d?h",
                             "\u0cb7\u0ccd",
                             "S",
                             "\u0cb7\u0ccd",
                             "z",
                             "\u0cb6\u0ccd",
                             "\u0cb8\u0ccd\u001d?h",
                             "\u0cb6\u0ccd",
                             "s",
                             "\u0cb8\u0ccd",
                             "v",
                             "\u0cb5\u0ccd",
                             "w",
                             "\u0cb5\u0ccd",
                             "L",
                             "\u0cb3\u0ccd",
                             "\\.L",
                             "\u0cde\u0ccd",
                             "l",
                             "\u0cb2\u0ccd",
                             "r",
                             "\u0cb0\u0ccd",
                             "\\.r",
                             "\u0cb1\u0ccd",
                             "y",
                             "\u0caf\u0ccd",
                             "~N",
                             "\u0c99\u0ccd",
                             "\u0c97\u0ccd\u001d?h",
                             "\u0c98\u0ccd",
                             "G",
                             "\u0c98\u0ccd",
                             "\\.g",
                             "\u0c97\u0cbc\u0ccd",
                             "g",
                             "\u0c97\u0ccd",
                             "\\.K",
                             "\u0c96\u0cbc\u0ccd",
                             "K",
                             "\u0c96\u0ccd",
                             "\u0c95\u0ccd\u001d?h",
                             "\u0c96\u0ccd",
                             "q",
                             "\u0c95\u0cbc\u0ccd",
                             "k",
                             "\u0c95\u0ccd",
                             "~n",
                             "\u0c9e\u0ccd",
                             "\u0c9c\u0ccd\u001d?h",
                             "\u0c9d\u0ccd",
                             "J",
                             "\u0c9d\u0ccd",
                             "\\.j",
                             "\u0c9c\u0cbc\u0ccd",
                             "j",
                             "\u0c9c\u0ccd",
                             "\u0c9a\u0ccd\u001d?h",
                             "\u0c9b\u0ccd",
                             "Ch",
                             "\u0c9b\u0ccd",
                             "ch",
                             "\u0c9a\u0ccd",
                             "C",
                             "\u0c9b\u0ccd",
                             "c",
                             "\u0c9a\u0ccd",
                             "N",
                             "\u0ca3\u0ccd",
                             "\u0ca1\u0cbc\u0ccd\u001d?h",
                             "\u0ca2\u0cbc\u0ccd",
                             "\\.D",
                             "\u0ca1\u0cbc\u0ccd",
                             "\u0ca1\u0ccd\u001d?h",
                             "\u0ca2\u0ccd",
                             "D",
                             "\u0ca1\u0ccd",
                             "\u0c9f\u0ccd\u001d?h",
                             "\u0ca0\u0ccd",
                             "T",
                             "\u0c9f\u0ccd",
                             "n",
                             "\u0ca8\u0ccd",
                             "\u0ca6\u0ccd\u001d?h",
                             "\u0ca7\u0ccd",
                             "d",
                             "\u0ca6\u0ccd",
                             "\u0ca4\u0ccd\u001d?h",
                             "\u0ca5\u0ccd",
                             "t",
                             "\u0ca4\u0ccd",
                             "m",
                             "\u0cae\u0ccd",
                             "\u0cac\u0ccd\u001d?h",
                             "\u0cad\u0ccd",
                             "B",
                             "\u0cad\u0ccd",
                             "b",
                             "\u0cac\u0ccd",
                             "f",
                             "\u0cab\u0cbc\u0ccd",
                             "\u0caa\u0ccd\u001d?h",
                             "\u0cab\u0ccd",
                             "P",
                             "\u0cab\u0ccd",
                             "p",
                             "\u0caa\u0ccd",
                             "\u0ca8\u0ccd\u001d?G",
                             "\u0c82\u0c98\u0ccd",
                             "\u0ca8\u0ccd\u001d?g",
                             "\u0c82\u0c97\u0ccd",
                             "\u0ca8\u0ccd\u001d?K",
                             "\u0c82\u0c96\u0ccd",
                             "\u0ca8\u0ccd\u001d?k",
                             "\u0c82\u0c95\u0ccd",
                             "\u0ca8\u0ccd\u001d?J",
                             "\u0c82\u0c9d\u0ccd",
                             "\u0ca8\u0ccd\u001d?j",
                             "\u0c82\u0c9c\u0ccd",
                             "\u0ca8\u0ccd\u001d?Ch",
                             "\u0c82\u0c9b\u0ccd",
                             "\u0ca8\u0ccd\u001d?ch",
                             "\u0c82\u0c9a\u0ccd",
                             "\u0ca8\u0ccd\u001d?C",
                             "\u0c82\u0c9b\u0ccd",
                             "\u0ca8\u0ccd\u001d?c",
                             "\u0c82\u0c9a\u0ccd",
                             "\u0ca8\u0ccd\u001d?D",
                             "\u0c82\u0ca1\u0ccd",
                             "\u0ca8\u0ccd\u001d?T",
                             "\u0c82\u0c9f\u0ccd",
                             "\u0ca8\u0ccd\u0ca8\u0ccd\u001d?g",
                             "\u0c99\u0ccd\u0c97\u0ccd",
                             "\u0ca8\u0ccd\u0ca8\u0ccd\u001d?k",
                             "\u0c99\u0ccd\u0c95\u0ccd",
                             "\u0ca8\u0ccd\u0ca8\u0ccd\u001d?j",
                             "\u0c9e\u0ccd\u0c9c\u0ccd",
                             "\u0ca8\u0ccd\u0ca8\u0ccd\u001d?Ch",
                             "\u0c9e\u0ccd\u0c9b\u0ccd",
                             "\u0ca8\u0ccd\u0ca8\u0ccd\u001d?ch",
                             "\u0c9e\u0ccd\u0c9a\u0ccd",
                             "\u0ca8\u0ccd\u0ca8\u0ccd\u001d?C",
                             "\u0c9e\u0ccd\u0c9b\u0ccd",
                             "\u0ca8\u0ccd\u0ca8\u0ccd\u001d?c",
                             "\u0c9e\u0ccd\u0c9a\u0ccd",
                             "\u0ca8\u0ccd\u0ca8\u0ccd\u001d?D",
                             "\u0ca3\u0ccd\u0ca1\u0ccd",
                             "\u0ca8\u0ccd\u0ca8\u0ccd\u001d?T",
                             "\u0ca3\u0ccd\u0c9f\u0ccd",
                             "\\|",
                             "\u0964",
                             "\u0964\u001d?\\|",
                             "\u0965"};
const unsigned int kTransformsLen = std::size(kTransforms);
const char* kHistoryPrune = "C|c";

}  // namespace kn_phone