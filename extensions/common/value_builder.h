// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides a builders for base::Value::Dict and base::Value::List.
// These aren't specific to extensions and could move up to base/ if there's
// interest from other sub-projects.
//
// The pattern is to write:
//
//  base::Value::T result(FooBuilder()
//                   .Set(args)
//                   .Set(args)
//                   .BuildT());
//
// The BuildT() method invalidates its builder, and returns ownership of the
// built value.
//
// The DEPRECATED pattern previously used was:
//
//  std::unique_ptr<BuiltType> result(FooBuilder()
//                               .Set(args)
//                               .Set(args)
//                               .Build());
//
// The Build() method invalidates its builder, and returns ownership of the
// built value.
//
// These objects are intended to be used as temporaries rather than stored
// anywhere, so the use of non-const reference parameters is likely to cause
// less confusion than usual.

#ifndef EXTENSIONS_COMMON_VALUE_BUILDER_H_
#define EXTENSIONS_COMMON_VALUE_BUILDER_H_

#include <memory>
#include <string>
#include <utility>

#include "base/strings/string_piece_forward.h"
#include "base/values.h"

namespace extensions {

class DictionaryBuilder {
 public:
  DictionaryBuilder();
  explicit DictionaryBuilder(const base::DictionaryValue& init);

  DictionaryBuilder(const DictionaryBuilder&) = delete;
  DictionaryBuilder& operator=(const DictionaryBuilder&) = delete;

  ~DictionaryBuilder();

  // Can only be called once, after which it's invalid to use the builder.
  base::Value::Dict BuildDict() {
    base::Value::Dict result = std::move(*dict_).TakeDict();
    dict_.reset();
    return result;
  }

  // DEPRECATED version of BuildDict().
  std::unique_ptr<base::DictionaryValue> Build() { return std::move(dict_); }

  // Immediately serializes the current state to JSON. Can be called as many
  // times as you like.
  std::string ToJSON() const;

  template <typename T>
  DictionaryBuilder& Set(base::StringPiece key, T in_value) {
    dict_->GetDict().Set(key, std::move(in_value));
    return *this;
  }

  // NOTE(devlin): This overload is really just for passing
  // std::unique_ptr<base::[SomeTypeOf]Value>, but the argument resolution
  // would require us to define a template specialization for each of the value
  // types. Just define this; it will fail to compile if <T> is anything but
  // a base::Value (or one of its subclasses).
  template <typename T>
  DictionaryBuilder& Set(base::StringPiece key, std::unique_ptr<T> in_value) {
    dict_->SetKey(key, std::move(*in_value));
    return *this;
  }

 private:
  std::unique_ptr<base::DictionaryValue> dict_;
};

class ListBuilder {
 public:
  ListBuilder();

  ListBuilder(const ListBuilder&) = delete;
  ListBuilder& operator=(const ListBuilder&) = delete;

  ~ListBuilder();

  // Can only be called once, after which it's invalid to use the builder.
  base::Value::List BuildList() {
    base::Value::List result = std::move(*list_).TakeList();
    list_.reset();
    return result;
  }

  // DEPRECATED version of BuildList().
  std::unique_ptr<base::ListValue> Build() { return std::move(list_); }

  template <typename T>
  ListBuilder& Append(T in_value) {
    list_->Append(std::move(in_value));
    return *this;
  }

  // Utility for appending a collection. Is this templating simplistic? Yes.
  // But if it's good enough for the STL, it's good enough for this class.
  template <typename InputIt>
  ListBuilder& Append(InputIt first, InputIt last) {
    for (; first != last; ++first)
      list_->Append(*first);
    return *this;
  }

  // See note on DictionaryBuilder::Set().
  template <typename T>
  ListBuilder& Append(std::unique_ptr<T> in_value) {
    list_->Append(std::move(*in_value));
    return *this;
  }

 private:
  std::unique_ptr<base::ListValue> list_;
};

}  // namespace extensions

#endif  // EXTENSIONS_COMMON_VALUE_BUILDER_H_
