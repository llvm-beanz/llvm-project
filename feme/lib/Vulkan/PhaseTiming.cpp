//===- PhaseTiming.cpp - Opt-in wall-time accounting by phase ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See PhaseTiming.h's own file comment for the "which phase" motivation.
//
//===----------------------------------------------------------------------===//

#include "PhaseTiming.h"

#include <cstdlib>

namespace feme::vulkan {
namespace {

/// One accumulator pair (total nanoseconds, call count) per `Phase`.
struct PhaseTotals {
  std::atomic<uint64_t> Ns[static_cast<size_t>(Phase::Count)] = {};
  std::atomic<uint64_t> Calls[static_cast<size_t>(Phase::Count)] = {};
};

PhaseTotals &totals() {
  static PhaseTotals Totals;
  return Totals;
}

const char *phaseName(Phase P) {
  switch (P) {
  case Phase::ImageCopyExec:
    return "ImageCopyExec";
  case Phase::FenceWait:
    return "FenceWait";
  case Phase::PipelineCompile:
    return "PipelineCompile";
  case Phase::CommandBufferExec:
    return "CommandBufferExec";
  case Phase::Count:
    break;
  }
  return "<unknown>";
}

void reportPhaseTotals() {
  for (size_t I = 0; I != static_cast<size_t>(Phase::Count); ++I) {
    uint64_t Calls = totals().Calls[I].load(std::memory_order_relaxed);
    if (Calls == 0)
      continue; // Never timed this run; omit it from the report.
    uint64_t Ns = totals().Ns[I].load(std::memory_order_relaxed);
    double TotalMs = static_cast<double>(Ns) / 1.0e6;
    std::fprintf(stderr,
                 "[FEME_VULKAN_TIME_PHASES] %-16s %10llu calls  %10.3f ms "
                 "total  %10.3f ms/call\n",
                 phaseName(static_cast<Phase>(I)),
                 static_cast<unsigned long long>(Calls), TotalMs,
                 TotalMs / static_cast<double>(Calls));
  }
}

/// Cached once: whether `FEME_VULKAN_TIME_PHASES` is set, registering
/// reportPhaseTotals to run at process exit the first (and only) time
/// this is checked while enabled, so the report prints exactly once
/// regardless of how many `ScopedPhaseTimer`s run.
bool phaseTimingEnabled() {
  static const bool Enabled = [] {
    bool IsSet = std::getenv("FEME_VULKAN_TIME_PHASES") != nullptr;
    if (IsSet)
      std::atexit(reportPhaseTotals);
    return IsSet;
  }();
  return Enabled;
}

} // namespace

ScopedPhaseTimer::ScopedPhaseTimer(Phase P)
    : P(P), Enabled(phaseTimingEnabled()) {
  if (Enabled)
    Start = std::chrono::steady_clock::now();
}

ScopedPhaseTimer::~ScopedPhaseTimer() {
  if (!Enabled)
    return;
  auto Elapsed = std::chrono::steady_clock::now() - Start;
  size_t Index = static_cast<size_t>(P);
  totals().Ns[Index].fetch_add(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Elapsed).count(),
      std::memory_order_relaxed);
  totals().Calls[Index].fetch_add(1, std::memory_order_relaxed);
}

} // namespace feme::vulkan
