//===- CFGGen.h - Seeded generator for CFG-shaped shaders ---------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::generateCFGIR, roadmap milestone 5's layer
// 3 generator (see the "CFG restructurization test suite" section of
// feme/docs/FeMeCPUDesign.md): a small, seeded generator producing
// shader-shaped raised LLVM IR -- random nesting of uniform and divergent
// `if`s, loops with random break/continue placement, and, behind a flag,
// unstructured edges that make the result irreducible. Used by the
// `feme-cfg-gen` tool, its differential harness, and
// `feme-cpu-restructure-fuzzer`.
//
// Every generated block folds its own id into a per-invocation accumulator
// (a simple multiplicative hash, `acc = acc * 2654435761 + id`), stored to
// an `alloca` so arbitrarily-shaped control flow (including the
// irreducible shapes `AllowUnstructured` enables) never has to hand-place
// its own `phi` nodes -- `feme::cpu::PreparePass`'s `mem2reg` step promotes
// it back to SSA. At the end, that accumulator is written to a raw-buffer
// UAV at the invocation's own (word) index, so *the output buffer is a
// trace of the path each invocation took*, matching the design's rationale
// for why this makes a mismatch diagnosable rather than merely detectable.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_CFGGEN_H
#define FEME_TRANSFORMS_CPU_CFGGEN_H

#include <cstdint>
#include <string>

namespace feme::cpu {

/// Options controlling `generateCFGIR`'s output. See the file comment above.
struct CFGGenOptions {
  /// Seeds the generator's PRNG; the same seed always produces the same
  /// output, byte-for-byte, regardless of the standard library
  /// `generateCFGIR` is built against (see CFGGen.cpp's `chance`/`randInt`
  /// for why that second part needs its own hand-rolled distributions
  /// rather than `<random>`'s).
  uint64_t Seed = 0;
  /// How deeply constructs (`if`/loop/irreducible-edge) may nest.
  unsigned MaxDepth = 3;
  /// The maximum number of constructs (at any nesting level) the generator
  /// spends its budget on; bounds the output's size regardless of how the
  /// random choices along the way fall.
  unsigned MaxConstructs = 12;
  /// Whether a generated branch condition may be divergent
  /// (thread-id-derived) as well as uniform.
  bool AllowDivergent = true;
  /// Whether the generator may emit loops (with random break/continue
  /// placement).
  bool AllowLoops = true;
  /// Whether the generator may emit unstructured edges that make the
  /// result irreducible (a two-entry mutual cycle, bounded so it always
  /// terminates -- see CFGGen.cpp's `genIrreducible`). `-verify-structured`
  /// and the fuzzer (layers 2 and 4) exercise these against
  /// `feme::cpu::verifyStructured`'s structural postconditions; the
  /// differential harness (layer 3) exercises them against `--reference`
  /// only, not yet the normal (widened) pipeline (see the Status section's
  /// milestone 5 deviation note in feme/docs/FeMeCPUDesign.md, and the new
  /// P0 gap it links to in feme/docs/Roadmap.md).
  bool AllowUnstructured = false;
};

/// Generates the textual LLVM IR (see feme/test/Tools/feme-run's shaders
/// for the raised-IR shape this matches) for a shader-shaped compute entry
/// point named `main`, deterministic given \p Opts.Seed. See the file
/// comment above for what the generated shader computes.
std::string generateCFGIR(const CFGGenOptions &Opts);

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_CFGGEN_H
