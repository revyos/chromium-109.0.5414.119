// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_CHECK_H_
#define BASE_CHECK_H_

#include <iosfwd>

#include "base/base_export.h"
#include "base/compiler_specific.h"
#include "base/dcheck_is_on.h"
#include "base/debug/debugging_buildflags.h"
#include "base/immediate_crash.h"

// This header defines the CHECK, DCHECK, and DPCHECK macros.
//
// CHECK dies with a fatal error if its condition is not true. It is not
// controlled by NDEBUG, so the check will be executed regardless of compilation
// mode.
//
// DCHECK, the "debug mode" check, is enabled depending on NDEBUG and
// DCHECK_ALWAYS_ON, and its severity depends on DCHECK_IS_CONFIGURABLE.
//
// (D)PCHECK is like (D)CHECK, but includes the system error code (c.f.
// perror(3)).
//
// Additional information can be streamed to these macros and will be included
// in the log output if the condition doesn't hold (you may need to include
// <ostream>):
//
//   CHECK(condition) << "Additional info.";
//
// The condition is evaluated exactly once. Even in build modes where e.g.
// DCHECK is disabled, the condition and any stream arguments are still
// referenced to avoid warnings about unused variables and functions.
//
// For the (D)CHECK_EQ, etc. macros, see base/check_op.h. However, that header
// is *significantly* larger than check.h, so try to avoid including it in
// header files.

namespace logging {

// Class used to explicitly ignore an ostream, and optionally a boolean value.
class VoidifyStream {
 public:
  VoidifyStream() = default;
  explicit VoidifyStream(bool ignored) {}

  // This operator has lower precedence than << but higher than ?:
  void operator&(std::ostream&) {}
};

// Macro which uses but does not evaluate expr and any stream parameters.
#define EAT_CHECK_STREAM_PARAMS(expr) \
  true ? (void)0                      \
       : ::logging::VoidifyStream(expr) & (*::logging::g_swallow_stream)
BASE_EXPORT extern std::ostream* g_swallow_stream;

class CheckOpResult;
class LogMessage;

// Class used for raising a check error upon destruction.
class BASE_EXPORT CheckError {
 public:
  static CheckError Check(const char* file, int line, const char* condition);
  static CheckError CheckOp(const char* file, int line, CheckOpResult* result);

  static CheckError DCheck(const char* file, int line, const char* condition);
  static CheckError DCheckOp(const char* file, int line, CheckOpResult* result);

  static CheckError PCheck(const char* file, int line, const char* condition);
  static CheckError PCheck(const char* file, int line);

  static CheckError DPCheck(const char* file, int line, const char* condition);

  static CheckError NotImplemented(const char* file,
                                   int line,
                                   const char* function);

  static CheckError NotReached(const char* file, int line);

  // Stream for adding optional details to the error message.
  std::ostream& stream();

  NOMERGE NOT_TAIL_CALLED ~CheckError();

  CheckError(const CheckError&) = delete;
  CheckError& operator=(const CheckError&) = delete;

  template <typename T>
  std::ostream& operator<<(T&& streamed_type) {
    return stream() << streamed_type;
  }

 private:
  explicit CheckError(LogMessage* log_message);

  LogMessage* const log_message_;
};

// The 'switch' is used to prevent the 'else' from being ambiguous when the
// macro is used in an 'if' clause such as:
// if (a == 1)
//   CHECK(Foo());
//
// TODO(crbug.com/1380930): Remove the const bool when the blink-gc plugin has
// been updated to accept `if (LIKELY(!field_))` as well as `if (!field_)`.
#define CHECK_FUNCTION_IMPL(check_failure_invocation, condition)   \
  switch (0)                                                       \
  case 0:                                                          \
  default:                                                         \
    if (const bool checky_bool_lol = static_cast<bool>(condition); \
        LIKELY(ANALYZER_ASSUME_TRUE(checky_bool_lol)))             \
      ;                                                            \
    else                                                           \
      check_failure_invocation

#if defined(OFFICIAL_BUILD) && !defined(NDEBUG)
#error "Debug builds are not expected to be optimized as official builds."
#endif  // defined(OFFICIAL_BUILD) && !defined(NDEBUG)

#if defined(OFFICIAL_BUILD) && !DCHECK_IS_ON()
// Note that this uses IMMEDIATE_CRASH_ALWAYS_INLINE to force-inline in debug
// mode as well. See LoggingTest.CheckCausesDistinctBreakpoints.
[[noreturn]] IMMEDIATE_CRASH_ALWAYS_INLINE void CheckFailure() {
  base::ImmediateCrash();
}

// Discard log strings to reduce code bloat.
//
// This is not calling BreakDebugger since this is called frequently, and
// calling an out-of-line function instead of a noreturn inline macro prevents
// compiler optimizations.
#define CHECK(condition) \
  UNLIKELY(!(condition)) ? logging::CheckFailure() : EAT_CHECK_STREAM_PARAMS()

#define CHECK_WILL_STREAM() false

// Strip the conditional string from official builds.
#define PCHECK(condition)                                                \
  CHECK_FUNCTION_IMPL(::logging::CheckError::PCheck(__FILE__, __LINE__), \
                      condition)

#else

#define CHECK_WILL_STREAM() true

#define CHECK(condition) \
  CHECK_FUNCTION_IMPL(   \
      ::logging::CheckError::Check(__FILE__, __LINE__, #condition), condition)

#define PCHECK(condition)                                            \
  CHECK_FUNCTION_IMPL(                                               \
      ::logging::CheckError::PCheck(__FILE__, __LINE__, #condition), \
      condition)

#endif

#if DCHECK_IS_ON()

#define DCHECK(condition)                                            \
  CHECK_FUNCTION_IMPL(                                               \
      ::logging::CheckError::DCheck(__FILE__, __LINE__, #condition), \
      condition)
#define DPCHECK(condition)                                            \
  CHECK_FUNCTION_IMPL(                                                \
      ::logging::CheckError::DPCheck(__FILE__, __LINE__, #condition), \
      condition)

#else

#define DCHECK(condition) EAT_CHECK_STREAM_PARAMS(!(condition))
#define DPCHECK(condition) EAT_CHECK_STREAM_PARAMS(!(condition))

#endif

// Async signal safe checking mechanism.
BASE_EXPORT void RawCheck(const char* message);
BASE_EXPORT void RawError(const char* message);
#define RAW_CHECK(condition)                                 \
  do {                                                       \
    if (!(condition))                                        \
      ::logging::RawCheck("Check failed: " #condition "\n"); \
  } while (0)

}  // namespace logging

#endif  // BASE_CHECK_H_
