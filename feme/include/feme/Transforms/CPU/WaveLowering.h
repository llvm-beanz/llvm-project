//===- WaveLowering.h - CPU target Phase 5: wave/builtin lowering -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::WaveLoweringPass, "Phase 5: Wave and Builtin
// Lowering" in feme/docs/FeMeCPUDesign.md: lowering the widened `<W x T>`
// module's raised wave intrinsics (`llvm.{dx,spv}.wave.*`) and thread/group
// builtins into the vector/scalar operations the CPU target's runtime
// support library and entry wrapper (feme::cpu::EntryWrapperPass) expect.
//
// Roadmap milestone 4 implements the "builtin half": lowering the
// `feme.cpu.builtin.*` calls `feme::cpu::SIMDizePass` introduces (thread id,
// thread id in group, flattened thread id in group, lane index) into real
// `<W x i32>` arithmetic over the wave-body's group id/wave index
// parameters and a compile-time-constant lane iota. Roadmap milestone 8
// adds the "wave op half": lowering the `feme.cpu.wave.*` calls
// `feme::cpu::SIMDizePass` introduces for a raised wave intrinsic (other
// than `wave.getlaneindex`, a builtin) into the vector reduction/scan/
// broadcast arithmetic each one stands in for -- see WaveLowering.cpp's
// file comment for exactly which wave intrinsics that covers, and why some
// (`WaveActiveSum`, `WaveActiveBallot`, `WavePrefixSum`, ...) are still
// left untouched.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_WAVELOWERING_H
#define FEME_TRANSFORMS_CPU_WAVELOWERING_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Phase 5: lowers wave intrinsics and thread/group builtins in a widened
/// module. See the file comment above for current scope.
class WaveLoweringPass : public llvm::PassInfoMixin<WaveLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-lower-wave"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_WAVELOWERING_H
