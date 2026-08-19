//===- DiagnosticsTest.cpp - Opt-in ICD error logging tests --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (C4a) Covers `logCreationFailure`'s opt-in gate: silent by default (the
// message is discarded, not printed), and, when
// `FEME_VULKAN_LOG_CREATION_ERRORS` is set, printed to the supplied stream
// with the caller-supplied "what" prefix.
//
//===----------------------------------------------------------------------===//

#include "Diagnostics.h"

#include "llvm/Support/raw_ostream.h"

#include "gtest/gtest.h"

#include <cstdlib>

using namespace feme::vulkan;
using namespace llvm;

namespace {

class DiagnosticsTest : public ::testing::Test {
protected:
  void TearDown() override {
#if defined(_WIN32)
    _putenv("FEME_VULKAN_LOG_CREATION_ERRORS=");
#else
    unsetenv("FEME_VULKAN_LOG_CREATION_ERRORS");
#endif
  }

  static void setEnv(const char *Value) {
#if defined(_WIN32)
    _putenv((std::string("FEME_VULKAN_LOG_CREATION_ERRORS=") + Value).c_str());
#else
    setenv("FEME_VULKAN_LOG_CREATION_ERRORS", Value, /*overwrite=*/1);
#endif
  }
};

TEST_F(DiagnosticsTest, SilentByDefault) {
  std::string Buffer;
  raw_string_ostream OS(Buffer);
  logCreationFailure(createStringError(inconvertibleErrorCode(), "boom"),
                     "vkCreateGraphicsPipelines", OS);
  EXPECT_TRUE(Buffer.empty());
}

TEST_F(DiagnosticsTest, SilentWhenExplicitlyDisabled) {
  setEnv("0");
  std::string Buffer;
  raw_string_ostream OS(Buffer);
  logCreationFailure(createStringError(inconvertibleErrorCode(), "boom"),
                     "vkCreateGraphicsPipelines", OS);
  EXPECT_TRUE(Buffer.empty());
}

TEST_F(DiagnosticsTest, LogsWhenEnabled) {
  setEnv("1");
  std::string Buffer;
  raw_string_ostream OS(Buffer);
  logCreationFailure(createStringError(inconvertibleErrorCode(), "boom"),
                     "vkCreateGraphicsPipelines", OS);
  EXPECT_NE(Buffer.find("vkCreateGraphicsPipelines"), std::string::npos);
  EXPECT_NE(Buffer.find("boom"), std::string::npos);
}

} // namespace
