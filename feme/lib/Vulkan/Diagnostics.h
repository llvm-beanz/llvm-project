//===- Diagnostics.h - Opt-in ICD error logging ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (C4a) A `VkResult`-returning entrypoint has nowhere to put an
// `llvm::Error`'s message: the ABI has room for an enumerator only, so
// today's convention (see e.g. `vkCreateGraphicsPipelines`) is to
// `consumeError` it and return the narrowest applicable `VkResult`. That
// makes triaging *why* a call failed require reading the ICD's own source
// rather than its output -- exactly what
// feme/docs/Roadmap.md's C4a step calls out.
//
// `logCreationFailure` is the opt-in log callback
// FeMeVulkanDesign.md's "Error Handling and Security" section already
// names ("while preserving diagnostics for `VK_EXT_debug_utils` or an
// opt-in log callback") ahead of either being implemented: silent by
// default -- consistent with "Never print from reusable library code" --
// and, when explicitly enabled by the driver's host environment (not the
// Vulkan application, which has no portable way to set FeMe-specific
// state), printing the `llvm::Error`'s full message to `errs()` before
// discarding it. It does not mutate any process-global *diagnostic*
// state: the opt-in flag is read from the environment once and never
// written by this code, unlike a `feme::Context` diagnostic handler.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_DIAGNOSTICS_H
#define FEME_LIB_VULKAN_DIAGNOSTICS_H

#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

namespace feme::vulkan {

/// Logs \p Err's message to \p OS (`errs()` by default), prefixed with \p
/// What (e.g. "vkCreateGraphicsPipelines"), when the ICD's opt-in error
/// logging is enabled (`FEME_VULKAN_LOG_CREATION_ERRORS` set to anything
/// but empty or "0" in the host environment), then discards it either
/// way -- this is always safe to call in place of a bare `consumeError`.
/// \p OS is a parameter, not hardcoded, so a unit test can observe what
/// gets logged without redirecting the process's real stderr.
void logCreationFailure(llvm::Error Err, llvm::StringRef What,
                        llvm::raw_ostream &OS = llvm::errs());

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_DIAGNOSTICS_H
