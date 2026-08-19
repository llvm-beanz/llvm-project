//===- Diagnostics.cpp - Opt-in ICD error logging ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Diagnostics.h"

#include <cstdlib>

using namespace llvm;

namespace feme::vulkan {

/// Whether the environment requests error logging: unset, empty, or "0"
/// all mean disabled, matching the usual boolean-environment-variable
/// convention. Checked on every call rather than cached, since a pipeline
/// creation failure is rare enough that a `getenv` call costs nothing
/// measurable, and caching it would make the flag's effective value
/// depend on which call happened to run first (awkward for tests, and no
/// real driver ever toggles this mid-process anyway).
static bool creationErrorLoggingEnabled() {
  const char *Env = std::getenv("FEME_VULKAN_LOG_CREATION_ERRORS");
  return Env && *Env && StringRef(Env) != "0";
}

void logCreationFailure(Error Err, StringRef What, raw_ostream &OS) {
  if (!creationErrorLoggingEnabled()) {
    consumeError(std::move(Err));
    return;
  }
  logAllUnhandledErrors(std::move(Err), OS, (What + ": ").str());
}

} // namespace feme::vulkan
