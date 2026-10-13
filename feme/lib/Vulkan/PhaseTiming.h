//===- PhaseTiming.h - Opt-in wall-time accounting by phase ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (Roadmap L418) A single slow CTS case (e.g.
// `dEQP-VK.api.copy_and_blit.core.use_after_copy.*1024x1024x2*img2img_msaa`)
// could plausibly be slow for any of several unrelated reasons -- shader/
// pipeline compilation, the copy/blit's own CPU execution, or time spent
// blocked in `vkWaitForFences` -- each calling for a completely different
// fix. Neither `perf` nor `strace` is guaranteed available in every
// environment this ICD is profiled in, so this header provides a
// minimal, zero-dependency substitute: an opt-in (`FEME_VULKAN_TIME_PHASES`
// environment variable, same pattern as `FEME_DUMP_IR_PRECANON`/
// `FEME_VULKAN_LOG_CREATION_ERRORS`) wall-clock accumulator per named
// Phase, reported to stderr once at process exit. Disabled (the default),
// the only cost is one cached `getenv` check per `ScopedPhaseTimer`
// construction/destruction; enabled, every phase's total time and call
// count answers exactly the "which phase" question a slow case raises,
// without needing any external profiler.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_PHASETIMING_H
#define FEME_LIB_VULKAN_PHASETIMING_H

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace feme::vulkan {

/// The fixed set of phases `FEME_VULKAN_TIME_PHASES` can break a slow
/// case's wall time down among. Add new entries here (and to `phaseName`
/// in PhaseTiming.cpp) as new "which phase is this actually slow in"
/// questions come up; the exit-time report only ever prints a phase that
/// was actually timed at least once, so adding an entry nobody
/// instruments yet is harmless.
enum class Phase {
  ImageCopyExec,    ///< CPU execution of `runCopyImage`/`runCopyBufferToImage`/
                     ///< `runCopyImageToBuffer` (the copy/blit's own data
                     ///< movement, as opposed to validating or recording it).
  FenceWait,         ///< Time blocked inside `vkWaitForFences` waiting for a
                     ///< queue's own worker thread to finish.
  PipelineCompile,   ///< `vkCreateGraphicsPipelines`/`vkCreateComputePipelines`'s
                     ///< own shader translation and compilation.
  CommandBufferExec, ///< The entire `executeCommandBuffers` replay of one
                     ///< submission's command buffers on the `QueueExecutor`
                     ///< worker thread -- every recorded command, not just
                     ///< the copy/blit ones `ImageCopyExec` already covers
                     ///< -- bisecting whether a slow case's real cost is
                     ///< inside command execution at all, or entirely in
                     ///< scheduling/wait overhead around it (roadmap L418).
  Count
};

/// RAII wall-clock accumulator for one `Phase`: construct at the start of
/// the scope to time, let it destruct at the end. A no-op (beyond the one
/// cached `getenv` check) unless `FEME_VULKAN_TIME_PHASES` is set.
class ScopedPhaseTimer {
public:
  explicit ScopedPhaseTimer(Phase P);
  ~ScopedPhaseTimer();

  ScopedPhaseTimer(const ScopedPhaseTimer &) = delete;
  ScopedPhaseTimer &operator=(const ScopedPhaseTimer &) = delete;

private:
  Phase P;
  bool Enabled;
  std::chrono::steady_clock::time_point Start;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_PHASETIMING_H
