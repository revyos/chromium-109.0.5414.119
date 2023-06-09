// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copied and modified from
// https://chromium.googlesource.com/chromium/src/+/a3f9d4fac81fc86065d867ab08fa4912ddf662c7/headless/public/util/error_reporter.h
// Modifications include namespace and path.

#ifndef COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_DEVTOOLS_ERROR_REPORTER_H_
#define COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_DEVTOOLS_ERROR_REPORTER_H_

#include <string>
#include <vector>

#include "base/strings/string_piece.h"

namespace autofill_assistant {

// Tracks errors which are encountered while parsing client API types. Note that
// errors are only reported in debug builds (i.e., when DCHECK is enabled).
class ErrorReporter {
 public:
  ErrorReporter();
  ~ErrorReporter();

#if DCHECK_IS_ON()
  // Enter a new nested parsing context. It will initially have a null name.
  void Push();

  // Leave the current parsing context, returning to the previous one.
  void Pop();

  // Set the name of the current parsing context. |name| must be a string with
  // application lifetime.
  void SetName(const char* name);

  // Report an error in the current parsing context.
  void AddError(base::StringPiece description);

  // Returns true if any errors have been reported so far.
  bool HasErrors() const;

  // Returns a list of reported errors.
  const std::vector<std::string>& errors() const { return errors_; }

  // Returns a string containing all the errors concatenated together.
  std::string ToString() const;
#else   // DCHECK_IS_ON()
  void Push() {}
  void Pop() {}
  void SetName(const char* name) {}
  void AddError(base::StringPiece description) {}
  bool HasErrors() const { return false; }
  std::vector<std::string> errors() const { return {}; }
  std::string ToString() const { return ""; }
#endif  // DCHECK_IS_ON()

 private:
  std::vector<const char*> path_;
  std::vector<std::string> errors_;
};

}  // namespace autofill_assistant

#endif  // COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_DEVTOOLS_ERROR_REPORTER_H_
