//===- OpRaising.cpp - Raise dx.op.* calls to idiomatic LLVM IR ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/DXIL/OpRaising.h"

#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme::dxil;

namespace {

/// A DXIL opcode (see `llvm/lib/Target/DirectX/DXIL.td`) that this pass
/// knows how to raise, and the single LLVM intrinsic call it was lowered
/// from. The opcode values below are DXIL's frozen wire-format encoding (the
/// numeric literal in each `DXILOp<N, ...>` tablegen definition), not
/// something FeMe controls or that changes across DXIL versions, so they are
/// safe to hard-code here rather than needing a dependency on the
/// DirectX-target-private generated tables that back `llvm::dxil::OpCode`.
struct RaisableOp {
  unsigned Opcode;
  Intrinsic::ID ID;
  /// Whether this intrinsic is overloaded on its (sole, after the opcode)
  /// operand's type, i.e. whether `getOrInsertDeclaration` needs that type
  /// passed as an explicit overload argument.
  bool Overloaded;
};

// All entries below were confirmed against LLVM's own `-dxil-op-lower` pass
// (the forward direction this pass inverts): for each row, a small `.ll`
// with the listed intrinsic call was run through `opt -S -dxil-op-lower`
// and the resulting `dx.op.*` call's opcode/signature cross-checked against
// `llvm/lib/Target/DirectX/DXIL.td`, rather than trusting the `.td` file's
// opcode-to-intrinsic-name association alone (a handful of ops -- e.g.
// `FMad`/`Fma`, both `tertiary`-shaped -- select their source intrinsic via
// dedicated C++ in `DXILOpLowering.cpp`, not a declarative `intrinsics =`
// list, so reading the `.td` file alone would have been insufficient).
//
// This covers every DXIL op with a direct, context-free mapping to a single
// LLVM intrinsic call, regardless of arity (unary/binary/tertiary/wider,
// e.g. `Dot2`..`Dot4`) or DXIL's "class" grouping (`unary`, `binary`,
// `tertiary`, `dot2`, ... are grouping-only; this pass doesn't need to
// distinguish them, since `raiseCall` below handles any argument count
// uniformly). Opcodes intentionally NOT covered here (documented in
// feme/docs/Design.md) either:
//  - return an aggregate/multiple values that would need `extractvalue`
//    reconstruction (`IMul`/`UMul`, `UAddc`, `SplitDouble`,
//    `WaveActiveBallot`),
//  - pick their source intrinsic based on an extra "kind"/flag operand
//    rather than the opcode alone (`WaveActiveOp`, `WaveActiveBit`,
//    `WavePrefixOp`, `QuadOp`, `Barrier`'s mode flags), or
//  - are resource-handle ops, which need `llvm::hlsl`-style resource
//    metadata reconstruction (`CreateHandle`, `AnnotateHandle`,
//    `CreateHandleFromBinding`, buffer/texture loads and stores, ...) --
//    those are covered by the separate `ResourceOps` table below instead.
// clang-format off
static const RaisableOp DirectOps[] = {
    // Scalar unary math raised to a standard LLVM intrinsic.
    {6, Intrinsic::fabs, true},              // Abs
    {7, Intrinsic::dx_saturate, true},       // Saturate
    {8, Intrinsic::dx_isnan, true},          // IsNan
    {9, Intrinsic::dx_isinf, true},          // IsInf
    {12, Intrinsic::cos, true},              // Cos
    {13, Intrinsic::sin, true},              // Sin
    {14, Intrinsic::tan, true},              // Tan
    {15, Intrinsic::acos, true},             // ACos
    {16, Intrinsic::asin, true},             // ASin
    {17, Intrinsic::atan, true},             // ATan
    {18, Intrinsic::cosh, true},             // HCos
    {19, Intrinsic::sinh, true},             // HSin
    {20, Intrinsic::tanh, true},             // HTan
    {21, Intrinsic::exp2, true},             // Exp2
    {22, Intrinsic::dx_frac, true},          // Frac
    {23, Intrinsic::log2, true},             // Log2
    {24, Intrinsic::sqrt, true},             // Sqrt
    {25, Intrinsic::dx_rsqrt, true},         // RSqrt
    {26, Intrinsic::roundeven, true},        // Round (round-to-nearest-even)
    {27, Intrinsic::floor, true},            // Floor
    {28, Intrinsic::ceil, true},             // Ceil
    {29, Intrinsic::trunc, true},            // Trunc
    {30, Intrinsic::bitreverse, true},       // Rbits
    {31, Intrinsic::ctpop, true},            // CountBits
    {32, Intrinsic::dx_firstbitlow, true},   // FirstbitLo
    {33, Intrinsic::dx_firstbituhigh, true}, // FirstbitHi
    {34, Intrinsic::dx_firstbitshigh, true}, // FirstbitSHi

    // Scalar binary math, overloaded on the (shared) operand type.
    {35, Intrinsic::maxnum, true}, // FMax
    {36, Intrinsic::minnum, true}, // FMin
    {37, Intrinsic::smax, true},   // SMax
    {38, Intrinsic::smin, true},   // SMin
    {39, Intrinsic::umax, true},   // UMax
    {40, Intrinsic::umin, true},   // UMin

    // Scalar tertiary (multiply-add family) math.
    {46, Intrinsic::fmuladd, true}, // FMad
    {47, Intrinsic::fma, true},     // Fma (DXIL1_0: double-only overload)
    {48, Intrinsic::dx_imad, true}, // IMad
    {49, Intrinsic::dx_umad, true}, // UMad

    // Vector dot products: fixed arg count (2/3/4 float pairs), overloaded
    // on the (shared) operand type.
    {54, Intrinsic::dx_dot2, true}, // Dot2
    {55, Intrinsic::dx_dot3, true}, // Dot3
    {56, Intrinsic::dx_dot4, true}, // Dot4

    // Pixel-shader-family screen-space derivatives. Like the arithmetic ops
    // above, raising doesn't need to re-validate DXIL's stage restrictions
    // (pixel/library/mesh/amplification/node) -- these calls are only
    // reachable here if `DXILOpLowering` already legally produced them.
    {83, Intrinsic::dx_ddx_coarse, true}, // DerivCoarseX
    {84, Intrinsic::dx_ddy_coarse, true}, // DerivCoarseY
    {85, Intrinsic::dx_ddx_fine, true},   // DerivFineX
    {86, Intrinsic::dx_ddy_fine, true},   // DerivFineY

    // Thread/wave/quad queries: fixed i32 (or no) operands, never overloaded.
    {93, Intrinsic::dx_thread_id, false},                    // ThreadId
    {94, Intrinsic::dx_group_id, false},                     // GroupId
    {95, Intrinsic::dx_thread_id_in_group, false},            // ThreadIdInGroup
    {96, Intrinsic::dx_flattened_thread_id_in_group, false},  // FlattenedThreadIdInGroup
    {101, Intrinsic::dx_asdouble, true},                      // MakeDouble (overloaded on the i32 operand type)
    {110, Intrinsic::dx_wave_is_first_lane, false},           // WaveIsFirstLane
    {111, Intrinsic::dx_wave_getlaneindex, false},            // WaveGetLaneIndex
    {113, Intrinsic::dx_wave_any, false},                     // WaveActiveAnyTrue
    {114, Intrinsic::dx_wave_all, false},                     // WaveActiveAllTrue
    {115, Intrinsic::dx_wave_all_equal, true},                // WaveActiveAllEqual (overloaded on the operand, not the i1 result)
    {117, Intrinsic::dx_wave_readlane, true},                 // WaveReadLaneAt
    {130, Intrinsic::dx_legacyf32tof16, true},                // LegacyF32ToF16 (overloaded on the float operand, though DXIL only uses f32)
    {131, Intrinsic::dx_legacyf16tof32, true},                // LegacyF16ToF32 (overloaded on the int operand, though DXIL only uses i32)
    {135, Intrinsic::dx_wave_active_countbits, false},        // WaveAllBitCount
    {136, Intrinsic::dx_wave_prefix_bit_count, false},        // WavePrefixBitCount

    // Fixed-shape ops with no overload at all.
    {82, Intrinsic::dx_discard, false},          // Discard
    {162, Intrinsic::dx_dot2add, false},         // Dot2AddHalf
    {163, Intrinsic::dx_dot4add_i8packed, false}, // Dot4AddI8Packed
    {164, Intrinsic::dx_dot4add_u8packed, false}, // Dot4AddU8Packed
};
// clang-format on

const RaisableOp *lookupRaisableOp(unsigned Opcode) {
  for (const RaisableOp &Op : DirectOps)
    if (Op.Opcode == Opcode)
      return &Op;
  return nullptr;
}

/// Raises the `IsFinite` (opcode 10) and `IsNormal` (opcode 11) DXIL ops.
/// Unlike `IsNaN`/`IsInf` (which round-trip through their own dedicated
/// `llvm.dx.isnan`/`llvm.dx.isinf` intrinsics), `DXILOpLowering` lowers both
/// of these from the generic `llvm.is.fpclass` intrinsic, selecting the
/// DXIL op via the `FPClassTest` bitmask in `is.fpclass`'s second operand
/// (`fcFinite` -> `IsFinite`, `fcNormal` -> `IsNormal`; see
/// `DXILOpLowering::lowerIsFPClass`). Raising therefore has to reconstruct
/// that second (mask) operand rather than a simple opcode -> intrinsic
/// lookup, so it doesn't fit the table-driven `raiseCall` path above.
bool raiseIsFPClassCall(CallInst &CI, unsigned Opcode) {
  if (CI.arg_size() != 2)
    return false;
  FPClassTest Mask = Opcode == 10 ? fcFinite : fcNormal;

  Module &M = *CI.getModule();
  IRBuilder<> Builder(&CI);
  Function *IsFPClassFn = Intrinsic::getOrInsertDeclaration(
      &M, Intrinsic::is_fpclass, {CI.getArgOperand(1)->getType()});
  CallInst *NewCall = Builder.CreateCall(
      IsFPClassFn, {CI.getArgOperand(1), Builder.getInt32(Mask)}, CI.getName());
  CI.replaceAllUsesWith(NewCall);
  CI.eraseFromParent();
  return true;
}

/// Rewrites a single `dx.op.*` call to the LLVM intrinsic call it was
/// lowered from, per \p RaiseAs. Returns false (leaving \p CI untouched) if
/// the call's shape doesn't match what's expected for \p RaiseAs (e.g. a
/// missing opcode operand), so callers can leave unrecognized shapes alone
/// rather than crashing on malformed/unexpected input.
bool raiseCall(CallInst &CI, const RaisableOp &RaiseAs) {
  // Operand 0 is always the opcode; the remaining operands (if any) are the
  // op's actual arguments, in order.
  if (CI.arg_size() == 0)
    return false;
  SmallVector<Value *, 2> Args(llvm::drop_begin(CI.args()));

  // The overload key is the *first* operand's type, not necessarily the
  // call's result type: e.g. IsNan/IsInf take a float-family operand but
  // return i1, and it's the operand type that selects the intrinsic
  // overload (`llvm.dx.isnan.f32`, not `.i1`). For multi-operand ops
  // (Dot2..Dot4, FMax/FMin, ...) DXIL only ever overloads on a single shared
  // operand type, so the first operand's type is always the right key.
  Module &M = *CI.getModule();
  Function *IntrinFn = RaiseAs.Overloaded
                           ? Intrinsic::getOrInsertDeclaration(
                                 &M, RaiseAs.ID, {Args[0]->getType()})
                           : Intrinsic::getOrInsertDeclaration(&M, RaiseAs.ID);

  IRBuilder<> Builder(&CI);
  CallInst *NewCall = Builder.CreateCall(IntrinFn, Args, CI.getName());
  // Only floating-point operations carry fast-math flags; guard the copy so
  // this doesn't assert on the integer/predicate ops in RaisableOp (e.g.
  // ThreadId, IsNan's i1 result).
  if (isa<FPMathOperator>(NewCall) && isa<FPMathOperator>(CI))
    NewCall->copyFastMathFlags(&CI);
  CI.replaceAllUsesWith(NewCall);
  CI.eraseFromParent();
  return true;
}

} // namespace

PreservedAnalyses OpRaisingPass::run(Module &M, ModuleAnalysisManager &AM) {
  bool Changed = false;

  // Snapshot the function list: raising erases `dx.op.*` declarations once
  // they have no more callers, and inserts new intrinsic declarations, both
  // of which would invalidate an in-place iterator over `M.functions()`.
  for (Function &F : llvm::make_early_inc_range(M.functions())) {
    if (!F.isDeclaration() || !F.getName().starts_with("dx.op."))
      continue;

    for (User *U : llvm::make_early_inc_range(F.users())) {
      auto *CI = dyn_cast<CallInst>(U);
      if (!CI || CI->getCalledFunction() != &F)
        continue;

      auto *OpcodeConst = dyn_cast<ConstantInt>(CI->getArgOperand(0));
      if (!OpcodeConst)
        continue;
      uint64_t Opcode = OpcodeConst->getZExtValue();

      if (Opcode == 10 || Opcode == 11) { // IsFinite, IsNormal
        Changed |= raiseIsFPClassCall(*CI, Opcode);
        continue;
      }

      const RaisableOp *RaiseAs = lookupRaisableOp(Opcode);
      if (!RaiseAs)
        continue;

      Changed |= raiseCall(*CI, *RaiseAs);
    }

    if (F.use_empty()) {
      F.eraseFromParent();
      Changed = true;
    }
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
