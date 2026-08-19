//===- OpRaising.cpp - Raise dx.op.* calls to idiomatic LLVM IR ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/DXIL/OpRaising.h"

#include "ResourceMetadata.h"

#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/DXILResource.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/DXILABI.h"
#include <optional>

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
// uniformly). Opcodes intentionally NOT covered by *this* table (documented
// in feme/docs/Design.md) either:
//  - pick their source intrinsic based on an extra "kind"/flag operand
//    rather than the opcode alone -- `Barrier`'s mode flags are covered by
//    `raiseBarrierCall`/`RaisableBarriers` below (required for the CPU
//    target, see feme/docs/FeMeCPUDesign.md's "Raised IR prerequisites");
//    `WaveActiveOp`/`WavePrefixOp`'s reduce-kind/signedness flag pair is
//    covered by `raiseReduceOpCall`/`RaisableReduceOp`; `WaveActiveBit`'s
//    bitwise-op flag is covered by `raiseWaveActiveBitCall`/`RaisableBitOp`;
//    and `QuadOp`'s direction flag is covered by `raiseQuadOpCall`/
//    `RaisableQuadOp` (roadmap step R4) -- or
//  - are resource-handle ops, which need `llvm::hlsl`-style resource
//    metadata reconstruction (`CreateHandle`, `AnnotateHandle`,
//    `CreateHandleFromBinding`, buffer/texture loads and stores, ...) --
//    those are covered by the separate `ResourceOps` table below instead.
// Ops that return an aggregate needing `extractvalue` reconstruction
// (`IMul`/`UMul`, `UAddc`, `SplitDouble`, `WaveActiveBallot`) are covered by
// the separate `AggregateOps` table/`raiseAggregateCall` below instead of
// this one, since they need per-`extractvalue` rewriting rather than a
// whole-value `replaceAllUsesWith` (see `RaisableAggregateOp`'s comment).
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
    {112, Intrinsic::dx_wave_get_lane_count, false},          // WaveGetLaneCount
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

/// Maps a DXIL `dxil::ElementType` (the wire-format element type encoded in
/// `AnnotateHandle`'s `ResourceProperties` operand, see
/// `llvm/include/llvm/Support/DXILABI.h`) to the LLVM scalar type used as a
/// resource target extension type's element type parameter (see
/// `llvm/include/llvm/Analysis/DXILResource.h`'s `TypedBufferExtType`).
/// Returns nullptr for element kinds this pass doesn't (yet) reconstruct: the
/// UNORM/SNORM/packed-8x32 formats, which need extra format metadata beyond a
/// plain LLVM scalar type to round-trip faithfully.
Type *getElementLLVMType(dxil::ElementType ET, LLVMContext &Ctx) {
  switch (ET) {
  case dxil::ElementType::I1:
    return Type::getInt1Ty(Ctx);
  case dxil::ElementType::I16:
  case dxil::ElementType::U16:
    return Type::getInt16Ty(Ctx);
  case dxil::ElementType::I32:
  case dxil::ElementType::U32:
    return Type::getInt32Ty(Ctx);
  case dxil::ElementType::I64:
  case dxil::ElementType::U64:
    return Type::getInt64Ty(Ctx);
  case dxil::ElementType::F16:
    return Type::getHalfTy(Ctx);
  case dxil::ElementType::F32:
    return Type::getFloatTy(Ctx);
  case dxil::ElementType::F64:
    return Type::getDoubleTy(Ctx);
  default:
    return nullptr;
  }
}

/// Widens \p ScalarTy into the vector type a TypedBuffer's `Word1`
/// component-count field (bits 8-15; see `ResourceInfo::getAnnotateProps`)
/// calls for: e.g. a `RWBuffer<float4>` reports a `CompCount` of 4, not the
/// bare scalar `getElementLLVMType` returns on its own, so that width has to
/// be recovered here to reconstruct the element type raiseTypedBufferStore/
/// raiseTypedBufferLoad expect to see. Returns nullptr if \p ScalarTy itself
/// is null (an unreconstructed component type) or `CompCount` is 0, which
/// never occurs for a real typed resource.
Type *widenToTypedBufferElement(Type *ScalarTy, uint64_t Word1) {
  uint32_t CompCount = (Word1 >> 8) & 0xFF;
  if (!ScalarTy || CompCount == 0)
    return nullptr;
  return CompCount > 1 ? cast<Type>(FixedVectorType::get(ScalarTy, CompCount))
                       : ScalarTy;
}

/// `TypedBufferExtType::isSigned()` distinguishes signed/unsigned *integer*
/// formats (`I32` vs `U32`), which `dxil::ElementType` already encodes
/// directly -- so this only needs to special-case the `U*` element kinds.
/// For non-integer element types (float, and the not-yet-reconstructed
/// norm/packed kinds) DXIL's wire format has no separate signedness bit at
/// all, so `true` here is an inherent-to-the-format best effort, not a
/// recoverable fact -- it doesn't affect codegen for those element types.
bool isSignedElementType(dxil::ElementType ET) {
  switch (ET) {
  case dxil::ElementType::U16:
  case dxil::ElementType::U32:
  case dxil::ElementType::U64:
    return false;
  default:
    return true;
  }
}

/// Reads \p V as a constant `i32`/`i8`, or returns `std::nullopt` if it isn't
/// one (e.g. because the resource binding isn't fully constant-folded, which
/// this pass doesn't attempt to reason about further).
std::optional<uint64_t> getConstInt(const Value *V) {
  if (const auto *CI = dyn_cast<ConstantInt>(V))
    return CI->getZExtValue();
  return std::nullopt;
}

/// The `Barrier` DXIL op's (opcode 80) constant mode operand -> the LLVM
/// intrinsic it was lowered from (see `Barrier`'s `intrinsics` list in
/// `llvm/lib/Target/DirectX/DXIL.td`, which enumerates exactly these six
/// mode values -- this table is that list's inverse). `Barrier` is a
/// required raised operation for the CPU target (see the "Raised IR
/// prerequisites" section of feme/docs/FeMeCPUDesign.md): every one of
/// these six intrinsics is what feme::cpu::EntryWrapperPass's barrier
/// region splitting (Phase 6) will eventually consume.
struct RaisableBarrier {
  uint64_t Mode;
  Intrinsic::ID ID;
};
// clang-format off
static const RaisableBarrier RaisableBarriers[] = {
    {2, Intrinsic::dx_device_memory_barrier},
    {3, Intrinsic::dx_device_memory_barrier_with_group_sync},
    {8, Intrinsic::dx_group_memory_barrier},
    {9, Intrinsic::dx_group_memory_barrier_with_group_sync},
    {10, Intrinsic::dx_all_memory_barrier},
    {11, Intrinsic::dx_all_memory_barrier_with_group_sync},
};
// clang-format on

/// Raises a `dx.op.barrier` (opcode 80) call, selecting the LLVM intrinsic
/// via its constant mode operand rather than the opcode alone (see
/// `RaisableBarriers` above), so it doesn't fit the table-driven `raiseCall`
/// path.
bool raiseBarrierCall(CallInst &CI) {
  if (CI.arg_size() != 2)
    return false;
  std::optional<uint64_t> Mode = getConstInt(CI.getArgOperand(1));
  if (!Mode)
    return false;

  for (const RaisableBarrier &B : RaisableBarriers) {
    if (B.Mode != *Mode)
      continue;
    IRBuilder<> Builder(&CI);
    Function *BarrierFn =
        Intrinsic::getOrInsertDeclaration(CI.getModule(), B.ID);
    Builder.CreateCall(BarrierFn);
    CI.eraseFromParent();
    return true;
  }
  return false; // An unrecognized mode: leave the call unmodified.
}

/// The `WaveOpKind_*`/`SignedOpKind_*`/`WaveBitOpKind_*`/`QuadOpKind_*`
/// `defvar`s `WaveActiveOp`/`WaveActiveBit`/`WavePrefixOp`/`QuadOp` select
/// their source intrinsic with (see the `defvar`s of the same name in
/// `llvm/lib/Target/DirectX/DXIL.td`, alongside each op's definition).
/// Mirrored here as plain constants (rather than an `enum class`) so they
/// compare directly against the `uint64_t` `getConstInt` reads off the
/// call's flag operand(s).
constexpr uint64_t WaveOpKind_Sum = 0;
constexpr uint64_t WaveOpKind_Product = 1;
constexpr uint64_t WaveOpKind_Min = 2;
constexpr uint64_t WaveOpKind_Max = 3;

constexpr uint64_t WaveBitOpKind_And = 0;
constexpr uint64_t WaveBitOpKind_Or = 1;
constexpr uint64_t WaveBitOpKind_Xor = 2;

constexpr uint64_t SignedOpKind_Signed = 0;
constexpr uint64_t SignedOpKind_Unsigned = 1;

constexpr uint64_t QuadOpKind_ReadAcrossX = 0;
constexpr uint64_t QuadOpKind_ReadAcrossY = 1;
constexpr uint64_t QuadOpKind_ReadAcrossDiagonal = 2;

/// `WaveActiveOp` (opcode 119) and `WavePrefixOp` (opcode 121) both select
/// their source intrinsic from the same pair of Int8Ty flag operands -- a
/// `WaveOpKind_*` reduce/scan kind, then a `SignedOpKind_*` signedness --
/// rather than the opcode alone (see each op's `intrinsics` list in
/// `llvm/lib/Target/DirectX/DXIL.td`). `WavePrefixOp` only ever selects
/// `Sum`/`Product` (DXIL has no prefix min/max), so this table's four
/// `WavePrefixOp` rows are exactly that DXIL.td subset, not an omission
/// here.
struct RaisableReduceOp {
  unsigned Opcode;
  uint64_t OpKind;
  uint64_t SignKind;
  Intrinsic::ID ID;
};

// clang-format off
static const RaisableReduceOp ReduceOps[] = {
    // WaveActiveOp (119): reduces its operand across the whole wave.
    {119, WaveOpKind_Sum, SignedOpKind_Signed, Intrinsic::dx_wave_reduce_sum},
    {119, WaveOpKind_Sum, SignedOpKind_Unsigned, Intrinsic::dx_wave_reduce_usum},
    {119, WaveOpKind_Product, SignedOpKind_Signed, Intrinsic::dx_wave_product},
    {119, WaveOpKind_Product, SignedOpKind_Unsigned, Intrinsic::dx_wave_uproduct},
    {119, WaveOpKind_Max, SignedOpKind_Signed, Intrinsic::dx_wave_reduce_max},
    {119, WaveOpKind_Max, SignedOpKind_Unsigned, Intrinsic::dx_wave_reduce_umax},
    {119, WaveOpKind_Min, SignedOpKind_Signed, Intrinsic::dx_wave_reduce_min},
    {119, WaveOpKind_Min, SignedOpKind_Unsigned, Intrinsic::dx_wave_reduce_umin},
    // WavePrefixOp (121): an exclusive scan of the same reduction, one wave
    // lane at a time.
    {121, WaveOpKind_Sum, SignedOpKind_Signed, Intrinsic::dx_wave_prefix_sum},
    {121, WaveOpKind_Sum, SignedOpKind_Unsigned, Intrinsic::dx_wave_prefix_usum},
    {121, WaveOpKind_Product, SignedOpKind_Signed, Intrinsic::dx_wave_prefix_product},
    {121, WaveOpKind_Product, SignedOpKind_Unsigned, Intrinsic::dx_wave_prefix_uproduct},
};
// clang-format on

const RaisableReduceOp *lookupRaisableReduceOp(unsigned Opcode, uint64_t OpKind,
                                               uint64_t SignKind) {
  for (const RaisableReduceOp &Op : ReduceOps)
    if (Op.Opcode == Opcode && Op.OpKind == OpKind && Op.SignKind == SignKind)
      return &Op;
  return nullptr;
}

/// Raises a `dx.op.waveActiveOp`/`dx.op.wavePrefixOp` (opcodes 119/121)
/// call, selecting the LLVM intrinsic via its two constant flag operands
/// (see `RaisableReduceOp` above) rather than the opcode alone, so it
/// doesn't fit the table-driven `raiseCall` path. Both intrinsics are
/// overloaded on the value operand's type, matching `OverloadTy` in
/// `WaveActiveOp`/`WavePrefixOp`'s DXIL.td definition.
bool raiseReduceOpCall(CallInst &CI, unsigned Opcode) {
  if (CI.arg_size() != 4)
    return false;
  std::optional<uint64_t> OpKind = getConstInt(CI.getArgOperand(2));
  std::optional<uint64_t> SignKind = getConstInt(CI.getArgOperand(3));
  if (!OpKind || !SignKind)
    return false;
  const RaisableReduceOp *RaiseAs =
      lookupRaisableReduceOp(Opcode, *OpKind, *SignKind);
  if (!RaiseAs)
    return false; // An unrecognized flag combination: leave the call as-is.

  Value *Operand = CI.getArgOperand(1);
  Function *IntrinFn = Intrinsic::getOrInsertDeclaration(
      CI.getModule(), RaiseAs->ID, {Operand->getType()});
  IRBuilder<> Builder(&CI);
  CallInst *NewCall = Builder.CreateCall(IntrinFn, {Operand}, CI.getName());
  CI.replaceAllUsesWith(NewCall);
  CI.eraseFromParent();
  return true;
}

/// `WaveActiveBit` (opcode 120) selects its source intrinsic from a single
/// `WaveBitOpKind_*` flag operand (see its `intrinsics` list in DXIL.td).
struct RaisableBitOp {
  uint64_t BitOpKind;
  Intrinsic::ID ID;
};

static const RaisableBitOp BitOps[] = {
    {WaveBitOpKind_And, Intrinsic::dx_wave_reduce_and},
    {WaveBitOpKind_Or, Intrinsic::dx_wave_reduce_or},
    {WaveBitOpKind_Xor, Intrinsic::dx_wave_reduce_xor},
};

/// Raises a `dx.op.waveActiveBit` (opcode 120) call, selecting the LLVM
/// intrinsic via its constant `WaveBitOpKind_*` flag operand (see
/// `RaisableBitOp` above) rather than the opcode alone.
bool raiseWaveActiveBitCall(CallInst &CI) {
  if (CI.arg_size() != 3)
    return false;
  std::optional<uint64_t> BitOpKind = getConstInt(CI.getArgOperand(2));
  if (!BitOpKind)
    return false;

  for (const RaisableBitOp &Op : BitOps) {
    if (Op.BitOpKind != *BitOpKind)
      continue;
    Value *Operand = CI.getArgOperand(1);
    Function *IntrinFn = Intrinsic::getOrInsertDeclaration(
        CI.getModule(), Op.ID, {Operand->getType()});
    IRBuilder<> Builder(&CI);
    CallInst *NewCall = Builder.CreateCall(IntrinFn, {Operand}, CI.getName());
    CI.replaceAllUsesWith(NewCall);
    CI.eraseFromParent();
    return true;
  }
  return false; // An unrecognized flag value: leave the call as-is.
}

/// `QuadOp` (opcode 123) selects its source intrinsic from a single
/// `QuadOpKind_*` flag operand (see its `intrinsics` list in DXIL.td).
struct RaisableQuadOp {
  uint64_t QuadKind;
  Intrinsic::ID ID;
};

static const RaisableQuadOp QuadOps[] = {
    {QuadOpKind_ReadAcrossX, Intrinsic::dx_quad_read_across_x},
    {QuadOpKind_ReadAcrossY, Intrinsic::dx_quad_read_across_y},
    {QuadOpKind_ReadAcrossDiagonal, Intrinsic::dx_quad_read_across_diagonal},
};

/// Raises a `dx.op.quadOp` (opcode 123) call, selecting the LLVM intrinsic
/// via its constant `QuadOpKind_*` flag operand (see `RaisableQuadOp`
/// above) rather than the opcode alone. Note this only raises the op; the
/// FeMe CPU target does not yet lower the resulting `llvm.dx.quad.read.*`
/// calls (quad/derivative support is an explicit v1 non-goal -- see
/// feme/docs/FeMeCPUDesign.md's "Non-Goals" section).
bool raiseQuadOpCall(CallInst &CI) {
  if (CI.arg_size() != 3)
    return false;
  std::optional<uint64_t> QuadKind = getConstInt(CI.getArgOperand(2));
  if (!QuadKind)
    return false;

  for (const RaisableQuadOp &Op : QuadOps) {
    if (Op.QuadKind != *QuadKind)
      continue;
    Value *Operand = CI.getArgOperand(1);
    Function *IntrinFn = Intrinsic::getOrInsertDeclaration(
        CI.getModule(), Op.ID, {Operand->getType()});
    IRBuilder<> Builder(&CI);
    CallInst *NewCall = Builder.CreateCall(IntrinFn, {Operand}, CI.getName());
    CI.replaceAllUsesWith(NewCall);
    CI.eraseFromParent();
    return true;
  }
  return false; // An unrecognized flag value: leave the call as-is.
}

/// A DXIL opcode that returns an aggregate (a fixed-shape struct of two or
/// more scalars) needing `extractvalue` reconstruction, and the single LLVM
/// intrinsic call it was lowered from -- the general mechanism `IMul`/
/// `UMul`/`UAddc`/`SplitDouble`/`WaveActiveBallot` were all deferred for
/// together (see this pass's header comment). Unlike `RaisableOp` above,
/// the result here is a genuine multi-value struct on both sides
/// (`dx.types.twoi32`/`dx.types.i32c`/`dx.types.splitdouble`/
/// `dx.types.fouri32` on the DXIL side, an equivalent-shaped anonymous LLVM
/// struct on the intrinsic side), so raising cannot simply RAUW the whole
/// call the way `raiseCall` does: the two struct types are never
/// `llvm::Type`-identical (one is named, the other a literal struct
/// uniqued by shape), only "layout identical" the way
/// `DXILOpLowering::replaceNamedStructUses` checks for the forward
/// direction. Raising therefore rewrites each `extractvalue` of the old
/// call individually to read the new one instead (mirroring
/// `raiseTypedBufferLoad`/`raiseRawBufferLoad`'s per-`extractvalue`
/// rewriting), rather than replacing the aggregate value itself.
struct RaisableAggregateOp {
  unsigned Opcode;
  Intrinsic::ID ID;
  /// The call's total argument count, opcode operand included (e.g. 3 for
  /// `IMul`'s `(opcode, a, b)`).
  unsigned ArgCount;
  /// The result struct's field count (2 for `IMul`/`UMul`/`UAddc`/
  /// `SplitDouble`'s `{iN, iN}`, 4 for `WaveActiveBallot`'s `{i32 x 4}`).
  unsigned NumResults;
  /// Whether the intrinsic's overload type is the first operand's type
  /// (`IMul`/`UMul`/`UAddc`, all `LLVMMatchType`-shaped between their
  /// operands and result) rather than a fixed `i32` (`SplitDouble`'s
  /// result halves and `WaveActiveBallot`'s result words are always `i32`,
  /// regardless of the `double`/`i1` operand's own type).
  bool OverloadOnOperand;
};

// clang-format off
static const RaisableAggregateOp AggregateOps[] = {
    {41, Intrinsic::dx_imul, 3, 2, true},          // IMul
    {42, Intrinsic::dx_umul, 3, 2, true},          // UMul
    {44, Intrinsic::uadd_with_overflow, 3, 2, true}, // UAddc
    {102, Intrinsic::dx_splitdouble, 2, 2, false}, // SplitDouble
    {116, Intrinsic::dx_wave_ballot, 2, 4, false},  // WaveActiveBallot
};
// clang-format on

const RaisableAggregateOp *lookupRaisableAggregateOp(unsigned Opcode) {
  for (const RaisableAggregateOp &Op : AggregateOps)
    if (Op.Opcode == Opcode)
      return &Op;
  return nullptr;
}

/// Raises a `dx.op.*` call returning an aggregate, per \p RaiseAs (see
/// `RaisableAggregateOp` above). Declines (leaving \p CI untouched) if the
/// call's argument count doesn't match, or if any of its uses isn't a
/// single-index `extractvalue` naming one of the result's fields -- the
/// only shape `DXILOpLowering`'s forward direction ever produces (see
/// `replaceNamedStructUses`), so anything else is unexpected input this
/// pass doesn't try to reason about further.
bool raiseAggregateCall(CallInst &CI, const RaisableAggregateOp &RaiseAs) {
  if (CI.arg_size() != RaiseAs.ArgCount)
    return false;

  SmallVector<ExtractValueInst *, 4> Extracts;
  for (User *U : CI.users()) {
    auto *EV = dyn_cast<ExtractValueInst>(U);
    if (!EV || EV->getNumIndices() != 1 ||
        EV->getIndices()[0] >= RaiseAs.NumResults)
      return false;
    Extracts.push_back(EV);
  }

  // Operand 0 is always the opcode; the remaining operands are the op's
  // actual arguments, in order (see `raiseCall`'s identical comment).
  SmallVector<Value *, 2> Args(llvm::drop_begin(CI.args()));

  Module &M = *CI.getModule();
  Type *OverloadTy = RaiseAs.OverloadOnOperand
                         ? Args[0]->getType()
                         : Type::getInt32Ty(M.getContext());
  Function *IntrinFn =
      Intrinsic::getOrInsertDeclaration(&M, RaiseAs.ID, {OverloadTy});

  IRBuilder<> Builder(&CI);
  CallInst *NewCall = Builder.CreateCall(IntrinFn, Args, CI.getName());

  for (ExtractValueInst *EV : Extracts) {
    Builder.SetInsertPoint(EV);
    Value *Field = Builder.CreateExtractValue(NewCall, EV->getIndices()[0]);
    EV->replaceAllUsesWith(Field);
    EV->eraseFromParent();
  }
  CI.eraseFromParent();
  return true;
}

/// Builds a type of exactly \p SizeInBytes bytes' ABI alloc size to stand in
/// for the original `StructuredBuffer`/`CBuffer` element/layout struct type,
/// which DXIL's binding metadata doesn't carry (only that struct's size and,
/// for `StructuredBuffer` only, its alignment -- see
/// `ResourceTypeInfo::getStruct`/`getCBufferSize` in
/// `llvm/lib/Analysis/DXILResource.cpp`). Reconstructing a plausible-looking
/// but fake field layout would silently produce a handle type that doesn't
/// match what actually flowed through the real frontend, so this
/// deliberately does the opposite: an opaque byte-sized placeholder that
/// makes no claim about field structure, honest about being a
/// reconstruction. This is enough for the handle to round-trip back through
/// `-dxil-op-lower` with the original binding size (and, when \p
/// AlignLog2 is nonzero, alignment) intact, which is what matters for
/// re-targeting the IR -- nothing downstream of a not-yet-raised handle
/// (see `raiseResourceHandleFromBinding`'s cast-back below) inspects the
/// element type's field structure.
///
/// \p AlignLog2 is only meaningful for `StructuredBuffer` (`CBuffer`'s
/// `ResourceProperties` encoding carries no alignment bits at all, so
/// callers pass 0 for it, which this treats as "no alignment to recover").
/// Only the alignments HLSL structs actually produce in practice (a power
/// of two from 1 to 16, driven by their largest scalar/vector member) are
/// reconstructed precisely, via a leading field of that natural alignment
/// (an integer for 1/2/4/8 bytes, a `<4 x i32>` for 16, matching what
/// produces align-16 struct layout in DXIL's own data layout); anything
/// else -- or an \p SizeInBytes not a multiple of the alignment, which
/// shouldn't happen for a real struct's alloc size -- falls back to a plain
/// byte array (ABI alignment 1), which is always a conservative
/// under-approximation of the true alignment, never an unsafe
/// over-approximation.
Type *getOpaqueSizedType(LLVMContext &Ctx, uint32_t SizeInBytes,
                         uint32_t AlignLog2) {
  Type *Int8Ty = Type::getInt8Ty(Ctx);
  Type *AlignFieldTy = nullptr;
  switch (AlignLog2) {
  case 0:
    break; // No (or no recoverable) alignment: plain byte array below.
  case 1:
    AlignFieldTy = Type::getInt16Ty(Ctx);
    break;
  case 2:
    AlignFieldTy = Type::getInt32Ty(Ctx);
    break;
  case 3:
    AlignFieldTy = Type::getInt64Ty(Ctx);
    break;
  case 4:
    AlignFieldTy = FixedVectorType::get(Type::getInt32Ty(Ctx), 4);
    break;
  default:
    break; // Unrecognized alignment: fall back to the byte array too.
  }

  uint32_t AlignBytes = 1u << AlignLog2;
  if (!AlignFieldTy || SizeInBytes % AlignBytes != 0)
    return ArrayType::get(Int8Ty, SizeInBytes);
  if (SizeInBytes == AlignBytes)
    return AlignFieldTy;
  return StructType::get(
      Ctx, {AlignFieldTy, ArrayType::get(Int8Ty, SizeInBytes - AlignBytes)});
}

/// Returns whether \p Kind is one of the non-multisampled, non-feedback
/// texture dimensions (`Texture1D`.."TextureCubeArray`), which share
/// `TypedBuffer`'s component-type/count encoding and therefore raise to
/// `dx.Texture` via the same `widenToTypedBufferElement` decode (see
/// Design.md's "Decision: texture and sampler handle kinds": "`isTyped()`
/// returns true for every non-feedback texture kind, which is why the
/// component type/count/sample-count layout is shared with `TypedBuffer`").
bool isPlainTextureKind(dxil::ResourceKind Kind) {
  switch (Kind) {
  case dxil::ResourceKind::Texture1D:
  case dxil::ResourceKind::Texture1DArray:
  case dxil::ResourceKind::Texture2D:
  case dxil::ResourceKind::Texture2DArray:
  case dxil::ResourceKind::Texture3D:
  case dxil::ResourceKind::TextureCube:
  case dxil::ResourceKind::TextureCubeArray:
    return true;
  default:
    return false;
  }
}

/// Reconstructs the `target("dx.")` handle type an `AnnotateHandle`'s
/// `%dx.types.ResourceProperties` two-word constant (\p Word0/\p Word1)
/// describes -- the decode `raiseResourceHandleFromBinding` and
/// `raiseResourceHandleFromHeap` both need identically, factored out here so
/// the two paths cannot disagree. See Design.md's "Decision: texture and
/// sampler handle kinds" for the field layout and the raised handle type
/// table this implements verbatim: `TypedBuffer` and the plain texture
/// dimensions (`isPlainTextureKind`) share their component type/count decode
/// and raise to `dx.TypedBuffer`/`dx.Texture` respectively;
/// `Texture2DMS(Array)` additionally carries a sample count and raises to
/// `dx.MSTexture`; `FeedbackTexture2D(Array)` carries only a
/// `SamplerFeedbackType` (the whole of \p Word1) and raises to
/// `dx.FeedbackTexture`; `Sampler` raises to `dx.Sampler`, keyed off
/// `SamplerCmpOrHasCounter` (Word0 bit 15). An unstructured `RawBuffer`
/// (`ByteAddressBuffer`), `StructuredBuffer` and `CBuffer` are unchanged from
/// before this function existed. Returns nullptr for a resource kind or element
/// format this pass doesn't (yet) reconstruct: `TBuffer`,
/// `RTAccelerationStructure`, and any UNORM/SNORM/ packed typed element format
/// (see `getElementLLVMType`) on any of the typed/texture/multisample-texture
/// kinds.
TargetExtType *buildAnnotatedHandleType(LLVMContext &Ctx, uint64_t Word0,
                                        uint64_t Word1) {
  auto Kind = static_cast<dxil::ResourceKind>(Word0 & 0xFF);
  bool IsUAV = (Word0 >> 12) & 1;
  bool IsROV = (Word0 >> 13) & 1;

  if (Kind == dxil::ResourceKind::TypedBuffer || isPlainTextureKind(Kind)) {
    auto ElemKind = static_cast<dxil::ElementType>(Word1 & 0xFF);
    Type *ElemTy =
        widenToTypedBufferElement(getElementLLVMType(ElemKind, Ctx), Word1);
    if (!ElemTy)
      return nullptr;
    unsigned IsSigned = isSignedElementType(ElemKind);
    if (Kind == dxil::ResourceKind::TypedBuffer)
      return TargetExtType::get(Ctx, "dx.TypedBuffer", {ElemTy},
                                {static_cast<unsigned>(IsUAV),
                                 static_cast<unsigned>(IsROV), IsSigned});
    return TargetExtType::get(Ctx, "dx.Texture", {ElemTy},
                              {static_cast<unsigned>(IsUAV),
                               static_cast<unsigned>(IsROV), IsSigned,
                               static_cast<unsigned>(Kind)});
  }

  if (Kind == dxil::ResourceKind::Texture2DMS ||
      Kind == dxil::ResourceKind::Texture2DMSArray) {
    auto ElemKind = static_cast<dxil::ElementType>(Word1 & 0xFF);
    Type *ElemTy =
        widenToTypedBufferElement(getElementLLVMType(ElemKind, Ctx), Word1);
    if (!ElemTy)
      return nullptr;
    uint32_t SampleCount = (Word1 >> 16) & 0xFF;
    return TargetExtType::get(
        Ctx, "dx.MSTexture", {ElemTy},
        {static_cast<unsigned>(IsUAV), SampleCount,
         static_cast<unsigned>(isSignedElementType(ElemKind)),
         static_cast<unsigned>(Kind)});
  }

  if (Kind == dxil::ResourceKind::FeedbackTexture2D ||
      Kind == dxil::ResourceKind::FeedbackTexture2DArray) {
    // A feedback texture's whole `Word1` is its `SamplerFeedbackType`, not a
    // packed component-type/count/sample-count field (see the field table
    // in Design.md's "Decision" section).
    return TargetExtType::get(
        Ctx, "dx.FeedbackTexture", {},
        {static_cast<unsigned>(Word1), static_cast<unsigned>(Kind)});
  }

  if (Kind == dxil::ResourceKind::Sampler) {
    // `SamplerCmpOrHasCounter` is a single bit whose two values (0, 1)
    // already match `dxil::SamplerType::{Default,Comparison}`.
    unsigned SamplerTy = (Word0 >> 15) & 1;
    return TargetExtType::get(Ctx, "dx.Sampler", {}, {SamplerTy});
  }

  if (Kind == dxil::ResourceKind::RawBuffer && Word1 == 0)
    return TargetExtType::get(
        Ctx, "dx.RawBuffer", {Type::getInt8Ty(Ctx)},
        {static_cast<unsigned>(IsUAV), static_cast<unsigned>(IsROV)});

  if (Kind == dxil::ResourceKind::StructuredBuffer) {
    // Word0's AlignLog2 field (bits 8-11) is only populated for structured
    // buffers (see `ResourceInfo::getAnnotateProps`), and Word1 is the
    // element stride in bytes for this kind.
    uint32_t AlignLog2 = (Word0 >> 8) & 0xF;
    Type *ElemTy =
        getOpaqueSizedType(Ctx, static_cast<uint32_t>(Word1), AlignLog2);
    return TargetExtType::get(
        Ctx, "dx.RawBuffer", {ElemTy},
        {static_cast<unsigned>(IsUAV), static_cast<unsigned>(IsROV)});
  }

  if (Kind == dxil::ResourceKind::CBuffer) {
    // CBuffer's ResourceProperties encoding carries no alignment bits at
    // all (`AlignLog2` is only ever set for `StructuredBuffer`), so there's
    // nothing to recover beyond size here.
    Type *ElemTy = getOpaqueSizedType(Ctx, static_cast<uint32_t>(Word1), 0);
    return TargetExtType::get(Ctx, "dx.CBuffer", {ElemTy});
  }

  return nullptr; // TBuffer, RTAccelerationStructure: not (yet) reconstructed.
}

/// Raises a `dx.op.annotateHandle` (opcode 216) call whose handle operand is
/// a `dx.op.createHandleFromBinding` (opcode 217) call back into a single
/// `llvm.dx.resource.handlefrombinding` intrinsic call, reconstructing the
/// resource's `target("dx.")` handle type from the two ops' constant
/// `%dx.types.ResBind`/`%dx.types.ResourceProperties` struct operands via
/// `buildAnnotatedHandleType` (see that function for which resource kinds,
/// including texture and sampler, are reconstructed and which are not).
/// Since the buffer/texture load and store ops that would actually consume
/// this handle aren't all raised yet either, the reconstructed handle is
/// bridged back to the legacy `%dx.types.Handle` type via
/// `llvm.dx.resource.casthandle` -- the same "temporary" cast
/// `DXILOpLowering` itself uses for this exact purpose (see
/// `DXILOpLowering::createTmpHandleCast`) -- so the result stays valid IR.
bool raiseResourceHandleFromBinding(CallInst &AnnotateCI) {
  if (AnnotateCI.arg_size() != 3)
    return false;
  auto *HandleCI = dyn_cast<CallInst>(AnnotateCI.getArgOperand(1));
  Function *HandleFn = HandleCI ? HandleCI->getCalledFunction() : nullptr;
  if (!HandleFn ||
      !HandleFn->getName().starts_with("dx.op.createHandleFromBinding") ||
      HandleCI->arg_size() != 4)
    return false;
  std::optional<uint64_t> HandleOpcode =
      getConstInt(HandleCI->getArgOperand(0));
  if (HandleOpcode != 217)
    return false;

  auto *ResBind = dyn_cast<ConstantStruct>(HandleCI->getArgOperand(1));
  auto *ResProps = dyn_cast<ConstantStruct>(AnnotateCI.getArgOperand(2));
  if (!ResBind || ResBind->getNumOperands() != 4 || !ResProps ||
      ResProps->getNumOperands() != 2)
    return false;

  std::optional<uint64_t> LowerBound = getConstInt(ResBind->getOperand(0));
  std::optional<uint64_t> UpperBound = getConstInt(ResBind->getOperand(1));
  std::optional<uint64_t> Space = getConstInt(ResBind->getOperand(2));
  std::optional<uint64_t> Word0 = getConstInt(ResProps->getOperand(0));
  std::optional<uint64_t> Word1 = getConstInt(ResProps->getOperand(1));
  if (!LowerBound || !UpperBound || !Space || !Word0 || !Word1)
    return false;

  LLVMContext &Ctx = AnnotateCI.getContext();
  TargetExtType *HandleTy = buildAnnotatedHandleType(Ctx, *Word0, *Word1);
  if (!HandleTy)
    return false; // Unsupported resource kind: leave both ops unmodified.

  uint32_t Size = *UpperBound == std::numeric_limits<uint32_t>::max()
                      ? 0
                      : static_cast<uint32_t>(*UpperBound - *LowerBound + 1);

  IRBuilder<> Builder(&AnnotateCI);
  // `DXILOpLowering` biases the binding-relative index it passes to
  // `CreateHandleFromBinding` by `LowerBound` (see
  // `DXILOpLowering::lowerToBindAndAnnotateHandle`); undo that here rather
  // than pattern-matching the `add` it emits to do so, since subtracting a
  // known constant is exact regardless of whether the original index was
  // itself a constant (already folded away) or a runtime value.
  Value *Index = HandleCI->getArgOperand(2);
  if (*LowerBound != 0)
    Index = Builder.CreateSub(Index, Builder.getInt32(*LowerBound));

  Function *HandleFromBindingFn = Intrinsic::getOrInsertDeclaration(
      AnnotateCI.getModule(), Intrinsic::dx_resource_handlefrombinding,
      {HandleTy});
  Value *NewHandle = Builder.CreateCall(
      HandleFromBindingFn,
      {Builder.getInt32(*Space), Builder.getInt32(*LowerBound),
       Builder.getInt32(Size), Index,
       ConstantPointerNull::get(PointerType::getUnqual(Ctx))});

  Function *CastFn = Intrinsic::getOrInsertDeclaration(
      AnnotateCI.getModule(), Intrinsic::dx_resource_casthandle,
      {AnnotateCI.getType(), HandleTy});
  Value *CastBack =
      Builder.CreateCall(CastFn, {NewHandle}, AnnotateCI.getName());

  AnnotateCI.replaceAllUsesWith(CastBack);
  AnnotateCI.eraseFromParent();
  if (HandleCI->use_empty())
    HandleCI->eraseFromParent();
  return true;
}

/// Raises a `dx.op.annotateHandle` (opcode 216) call whose handle operand is
/// a `dx.op.createHandleFromHeap` (opcode 218) call into a single
/// `llvm.dx.resource.handlefromheap` intrinsic call -- the bindless
/// descriptor-heap counterpart of `raiseResourceHandleFromBinding` above
/// (SM 6.6+ dynamic resource indexing, `ResourceDescriptorHeap[i]`/
/// `SamplerDescriptorHeap[i]`; see the "Resource Model" section of
/// feme/docs/FeMeCPUDesign.md). This is required raised IR for the CPU
/// target, which accepts bindless shaders only.
///
/// Unlike `CreateHandleFromBinding`, there is no `%dx.types.ResBind` operand
/// to reconstruct a register binding from -- a heap index is not a
/// register -- so this only needs `AnnotateHandle`'s
/// `%dx.types.ResourceProperties` operand to recover the resource's
/// `target("dx.")` handle type, via `buildAnnotatedHandleType`, exactly as
/// `raiseResourceHandleFromBinding` does. The heap index and
/// non-uniform-index operands carry over unchanged. The raw op's
/// `SamplerHeap` operand (`CreateHandleFromHeap`'s second argument) does not
/// need to survive separately: which heap a handle indexes is already
/// implied by whether its reconstructed resource kind is `dx.Sampler`.
bool raiseResourceHandleFromHeap(CallInst &AnnotateCI) {
  if (AnnotateCI.arg_size() != 3)
    return false;
  auto *HandleCI = dyn_cast<CallInst>(AnnotateCI.getArgOperand(1));
  Function *HandleFn = HandleCI ? HandleCI->getCalledFunction() : nullptr;
  if (!HandleFn ||
      !HandleFn->getName().starts_with("dx.op.createHandleFromHeap") ||
      HandleCI->arg_size() != 4)
    return false;
  std::optional<uint64_t> HandleOpcode =
      getConstInt(HandleCI->getArgOperand(0));
  if (HandleOpcode != 218)
    return false;

  auto *ResProps = dyn_cast<ConstantStruct>(AnnotateCI.getArgOperand(2));
  if (!ResProps || ResProps->getNumOperands() != 2)
    return false;

  std::optional<uint64_t> Word0 = getConstInt(ResProps->getOperand(0));
  std::optional<uint64_t> Word1 = getConstInt(ResProps->getOperand(1));
  if (!Word0 || !Word1)
    return false;

  LLVMContext &Ctx = AnnotateCI.getContext();
  TargetExtType *HandleTy = buildAnnotatedHandleType(Ctx, *Word0, *Word1);
  if (!HandleTy)
    return false; // Unsupported resource kind: leave both ops unmodified.

  IRBuilder<> Builder(&AnnotateCI);
  Value *Index = HandleCI->getArgOperand(1);
  Value *NonUniform = HandleCI->getArgOperand(3);

  Function *HandleFromHeapFn = Intrinsic::getOrInsertDeclaration(
      AnnotateCI.getModule(), Intrinsic::dx_resource_handlefromheap,
      {HandleTy});
  Value *NewHandle = Builder.CreateCall(HandleFromHeapFn, {Index, NonUniform});

  Function *CastFn = Intrinsic::getOrInsertDeclaration(
      AnnotateCI.getModule(), Intrinsic::dx_resource_casthandle,
      {AnnotateCI.getType(), HandleTy});
  Value *CastBack =
      Builder.CreateCall(CastFn, {NewHandle}, AnnotateCI.getName());

  AnnotateCI.replaceAllUsesWith(CastBack);
  AnnotateCI.eraseFromParent();
  if (HandleCI->use_empty())
    HandleCI->eraseFromParent();
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

/// Returns the `target("dx.")` handle \p V was bridged back from, if \p V is
/// a `llvm.dx.resource.casthandle` call producing a legacy
/// `%dx.types.Handle` -- the bridge `raiseResourceHandleFromBinding`/
/// `raiseLegacyCreateHandle` leave behind so not-yet-raised consumers of the
/// handle stay valid IR. Returns nullptr otherwise.
Value *lookThroughCastHandle(Value *V) {
  auto *CI = dyn_cast<CallInst>(V);
  Function *F = CI ? CI->getCalledFunction() : nullptr;
  if (!F || F->getIntrinsicID() != Intrinsic::dx_resource_casthandle)
    return nullptr;
  return CI->getArgOperand(0);
}

/// Returns the number of leading components \p Mask selects, or 0 if it isn't
/// a contiguous low-order mask (DXIL's typed buffer stores always write a
/// contiguous run of components starting at 0, so anything else is a shape
/// this pass doesn't model).
unsigned getContiguousMaskWidth(uint64_t Mask) {
  unsigned Width = llvm::countr_one(Mask);
  return (Mask >> Width) == 0 ? Width : 0;
}

/// Infers the number of components in a typed buffer's or texture's element
/// type from the `dx.op.bufferStore`/`dx.op.bufferLoad` (or, for a texture,
/// `dx.op.textureStore`/`dx.op.textureLoad`) calls consuming \p HandleCI's
/// result. DXIL's metadata only records a typed buffer/texture's *component*
/// type (`float`), never its vector width (`<4 x float>`), so the width has
/// to be recovered from how the resource is actually accessed: a store's
/// write mask names it directly (always the *last* operand of both ops,
/// despite their differing arities -- `BufferStore` has no coordinate
/// `Coord2`, `TextureStore` does), and a load's `%dx.types.ResRet`
/// components are only extracted up to it. Defaults to 4 (DXIL's widest
/// typed buffer/texture element) when the handle has no accesses to learn
/// from.
unsigned inferTypedBufferWidth(const CallInst &HandleCI) {
  unsigned Width = 0;
  for (const User *U : HandleCI.users()) {
    const auto *CI = dyn_cast<CallInst>(U);
    const Function *Callee = CI ? CI->getCalledFunction() : nullptr;
    if (!Callee)
      continue;
    StringRef Name = Callee->getName();

    if (Name.starts_with("dx.op.bufferStore") ||
        Name.starts_with("dx.op.textureStore")) {
      if (std::optional<uint64_t> Mask =
              getConstInt(CI->getArgOperand(CI->arg_size() - 1)))
        Width = std::max(Width, getContiguousMaskWidth(*Mask));
      continue;
    }

    if (!Name.starts_with("dx.op.bufferLoad") &&
        !Name.starts_with("dx.op.textureLoad"))
      continue;
    for (const User *LoadUser : CI->users())
      if (const auto *EV = dyn_cast<ExtractValueInst>(LoadUser))
        if (EV->getNumIndices() == 1 && EV->getIndices()[0] < 4)
          Width = std::max(Width, EV->getIndices()[0] + 1);
  }
  return Width ? Width : 4;
}

/// Builds the `target("dx.")` handle type for \p Binding, or returns nullptr
/// for resource kinds this pass doesn't yet reconstruct (samplers,
/// multisampled/feedback textures, and the norm/packed typed buffer/texture
/// element formats -- see `getElementLLVMType`). \p VectorWidth is the typed
/// buffer/texture element vector width recovered by `inferTypedBufferWidth`
/// (legacy `!dx.resources` metadata records a texture's component *type* the
/// same way it does a typed buffer's, but never its component *count* --
/// see Design.md's "Decision: texture and sampler handle kinds" -- so both
/// need the same access-site recovery).
TargetExtType *buildHandleType(LLVMContext &Ctx, const ResourceBinding &Binding,
                               unsigned VectorWidth) {
  bool IsUAV = Binding.Class == dxil::ResourceClass::UAV;
  if (Binding.Kind == dxil::ResourceKind::TypedBuffer ||
      isPlainTextureKind(Binding.Kind)) {
    Type *ScalarTy = getElementLLVMType(Binding.ElementType, Ctx);
    if (!ScalarTy)
      return nullptr;
    Type *ElemTy = VectorWidth > 1
                       ? cast<Type>(FixedVectorType::get(ScalarTy, VectorWidth))
                       : ScalarTy;
    unsigned IsSigned = isSignedElementType(Binding.ElementType);
    if (Binding.Kind == dxil::ResourceKind::TypedBuffer)
      return TargetExtType::get(
          Ctx, "dx.TypedBuffer", {ElemTy},
          {static_cast<unsigned>(IsUAV), static_cast<unsigned>(Binding.IsROV),
           IsSigned});
    return TargetExtType::get(
        Ctx, "dx.Texture", {ElemTy},
        {static_cast<unsigned>(IsUAV), static_cast<unsigned>(Binding.IsROV),
         IsSigned, static_cast<unsigned>(Binding.Kind)});
  }

  switch (Binding.Kind) {
  case dxil::ResourceKind::RawBuffer:
    return TargetExtType::get(
        Ctx, "dx.RawBuffer", {Type::getInt8Ty(Ctx)},
        {static_cast<unsigned>(IsUAV), static_cast<unsigned>(Binding.IsROV)});
  case dxil::ResourceKind::StructuredBuffer:
    // `!dx.resources` records a structured buffer's element stride but not
    // its alignment, so there is nothing to recover beyond size here (see
    // `getOpaqueSizedType`).
    return TargetExtType::get(
        Ctx, "dx.RawBuffer", {getOpaqueSizedType(Ctx, Binding.StrideOrSize, 0)},
        {static_cast<unsigned>(IsUAV), static_cast<unsigned>(Binding.IsROV)});
  case dxil::ResourceKind::CBuffer:
    return TargetExtType::get(
        Ctx, "dx.CBuffer", {getOpaqueSizedType(Ctx, Binding.StrideOrSize, 0)});
  default:
    return nullptr;
  }
}

/// Raises a pre-SM6.6 `dx.op.createHandle` (opcode 57) call into a
/// `llvm.dx.resource.handlefrombinding` intrinsic call. Unlike the newer
/// `CreateHandleFromBinding`/`AnnotateHandle` pair (see
/// `raiseResourceHandleFromBinding`), this op carries no binding information
/// inline: it names its resource indirectly, by (resource class, range ID),
/// which is why \p MD -- the module's `!dx.resources` metadata -- is needed.
///
/// The reconstructed handle is bridged back to `%dx.types.Handle` via
/// `llvm.dx.resource.casthandle` so that any consumer this pass does not
/// raise stays valid IR; `raiseTypedBufferAccess` looks back through that
/// bridge, and dead bridges are cleaned up afterwards.
bool raiseLegacyCreateHandle(CallInst &CI, const ResourceMetadata &MD) {
  if (CI.arg_size() != 5)
    return false;
  std::optional<uint64_t> Class = getConstInt(CI.getArgOperand(1));
  std::optional<uint64_t> RangeID = getConstInt(CI.getArgOperand(2));
  if (!Class ||
      *Class > static_cast<uint64_t>(dxil::ResourceClass::LastEntry) ||
      !RangeID)
    return false;

  std::optional<ResourceBinding> Binding =
      MD.lookup(static_cast<dxil::ResourceClass>(*Class),
                static_cast<uint32_t>(*RangeID));
  if (!Binding)
    return false;

  LLVMContext &Ctx = CI.getContext();
  TargetExtType *HandleTy =
      buildHandleType(Ctx, *Binding, inferTypedBufferWidth(CI));
  if (!HandleTy)
    return false;

  IRBuilder<> Builder(&CI);
  // `CreateHandle`'s index operand is the resource's absolute register index,
  // while `llvm.dx.resource.handlefrombinding`'s is relative to the binding's
  // lower bound; rebase it rather than pattern-matching whatever computed it.
  Value *Index = CI.getArgOperand(3);
  if (Binding->LowerBound != 0)
    Index = Builder.CreateSub(Index, Builder.getInt32(Binding->LowerBound));

  Function *HandleFromBindingFn = Intrinsic::getOrInsertDeclaration(
      CI.getModule(), Intrinsic::dx_resource_handlefrombinding, {HandleTy});
  Value *NewHandle = Builder.CreateCall(
      HandleFromBindingFn,
      {Builder.getInt32(Binding->Space), Builder.getInt32(Binding->LowerBound),
       Builder.getInt32(Binding->RangeSize), Index,
       ConstantPointerNull::get(PointerType::getUnqual(Ctx))});

  Function *CastFn = Intrinsic::getOrInsertDeclaration(
      CI.getModule(), Intrinsic::dx_resource_casthandle,
      {CI.getType(), HandleTy});
  Value *CastBack = Builder.CreateCall(CastFn, {NewHandle}, CI.getName());
  CI.replaceAllUsesWith(CastBack);
  CI.eraseFromParent();
  return true;
}

/// Raises a `dx.op.bufferStore` (opcode 69) call on an already-raised typed
/// buffer handle into `llvm.dx.resource.store.typedbuffer`, reassembling the
/// four scalar component operands DXIL splits the stored value into back into
/// the handle's element type.
bool raiseTypedBufferStore(CallInst &CI) {
  if (CI.arg_size() != 9)
    return false;
  Value *Handle = lookThroughCastHandle(CI.getArgOperand(1));
  auto *HandleTy =
      Handle ? dyn_cast<TargetExtType>(Handle->getType()) : nullptr;
  if (!HandleTy || HandleTy->getName() != "dx.TypedBuffer")
    return false;

  std::optional<uint64_t> Mask = getConstInt(CI.getArgOperand(8));
  if (!Mask)
    return false;
  unsigned Width = getContiguousMaskWidth(*Mask);
  Type *ElemTy = HandleTy->getTypeParameter(0);
  auto *VecTy = dyn_cast<FixedVectorType>(ElemTy);
  if (Width != (VecTy ? VecTy->getNumElements() : 1))
    return false;

  IRBuilder<> Builder(&CI);
  Value *Stored = CI.getArgOperand(4);
  if (VecTy) {
    Stored = PoisonValue::get(VecTy);
    for (unsigned I = 0; I != Width; ++I)
      Stored = Builder.CreateInsertElement(Stored, CI.getArgOperand(4 + I),
                                           Builder.getInt32(I));
  }
  if (Stored->getType() != ElemTy)
    return false;

  Function *StoreFn = Intrinsic::getOrInsertDeclaration(
      CI.getModule(), Intrinsic::dx_resource_store_typedbuffer,
      {HandleTy, ElemTy});
  Builder.CreateCall(StoreFn, {Handle, CI.getArgOperand(2), Stored});
  CI.eraseFromParent();
  return true;
}

/// Raises a `dx.op.bufferLoad` (opcode 68) call on an already-raised typed
/// buffer handle into `llvm.dx.resource.load.typedbuffer`, rewriting the
/// `extractvalue`s of DXIL's `%dx.types.ResRet` return struct into
/// `extractelement`s of the loaded vector. Loads whose result is consumed in
/// any other way -- notably ones reading `ResRet`'s trailing status field,
/// which has no equivalent in the raised intrinsic's `i1` "checkbit" -- are
/// left unraised.
bool raiseTypedBufferLoad(CallInst &CI) {
  if (CI.arg_size() != 4)
    return false;
  Value *Handle = lookThroughCastHandle(CI.getArgOperand(1));
  auto *HandleTy =
      Handle ? dyn_cast<TargetExtType>(Handle->getType()) : nullptr;
  if (!HandleTy || HandleTy->getName() != "dx.TypedBuffer")
    return false;

  Type *ElemTy = HandleTy->getTypeParameter(0);
  auto *VecTy = dyn_cast<FixedVectorType>(ElemTy);
  unsigned Width = VecTy ? VecTy->getNumElements() : 1;

  SmallVector<ExtractValueInst *, 4> Extracts;
  for (User *U : CI.users()) {
    auto *EV = dyn_cast<ExtractValueInst>(U);
    if (!EV || EV->getNumIndices() != 1 || EV->getIndices()[0] >= Width)
      return false;
    Extracts.push_back(EV);
  }

  IRBuilder<> Builder(&CI);
  Function *LoadFn = Intrinsic::getOrInsertDeclaration(
      CI.getModule(), Intrinsic::dx_resource_load_typedbuffer,
      {ElemTy, HandleTy});
  Value *Loaded = Builder.CreateCall(LoadFn, {Handle, CI.getArgOperand(2)});
  Value *Value0 = Builder.CreateExtractValue(Loaded, 0);

  for (ExtractValueInst *EV : Extracts) {
    Builder.SetInsertPoint(EV);
    Value *Component =
        VecTy ? Builder.CreateExtractElement(Value0, EV->getIndices()[0])
              : Value0;
    EV->replaceAllUsesWith(Component);
    EV->eraseFromParent();
  }
  CI.eraseFromParent();
  return true;
}

//===----------------------------------------------------------------------===//
// Texture sampling and loading (roadmap R30)
//
// Scope: only the DXIL ops LLVM's own DirectX backend already lowers a
// canonical intrinsic to (cross-checked against `-dxil-op-lower`, the same
// standard every other raiser in this file holds itself to -- see the file
// header comment): `Sample` (60), `SampleLevel` (62), `TextureLoad` (66) and
// `GetDimensions`' `.x` field (72, via `int_dx_resource_getdimensions_x`
// only -- `DXILOpLowering.cpp` has no `xy`/`levels_xy` lowering to verify
// against yet). `SampleBias`/`SampleGrad` (bias/gradient sampling) and
// comparison sampling/gather have no numbered `DXILOp<N, ...>` definition in
// this LLVM tree at all yet (`sampleCmp`/`textureGather` are declared
// `DXILOpClass`es with no op assigned a wire opcode), so there is nothing to
// raise from or verify against on the DXIL side; comparison sampling is
// still implemented end-to-end for SPIR-V and the CPU target's runtime
// helpers (see feme/docs/FeMeGraphicsDesign.md's "Canonical image
// operations"), just not reachable from a DXIL module until upstream adds
// that lowering.
//===----------------------------------------------------------------------===//

/// The number of texture coordinate components DXIL's sample/load ops
/// expect for \p Dim, i.e. the width of the vector `int_dx_resource_sample*`
/// /`load_level`'s `coord` operand packs (see Design.md's "Decision: texture
/// and sampler handle kinds" -- `Dim` is exactly `TextureExtType::
/// getDimension()`). An array dimension adds one component (the array
/// slice) beyond its non-array counterpart. Returns 0 for a dimension this
/// pass does not raise texture accesses for (multisampled and feedback
/// textures; MSTexture's `Load` op takes a sample index DXIL encodes
/// differently, and no feedback-texture op is raised at all yet).
unsigned getTextureCoordComponents(dxil::ResourceKind Dim) {
  switch (Dim) {
  case dxil::ResourceKind::Texture1D:
    return 1;
  case dxil::ResourceKind::Texture1DArray:
    return 2;
  case dxil::ResourceKind::Texture2D:
    return 2;
  case dxil::ResourceKind::Texture2DArray:
    return 3;
  case dxil::ResourceKind::Texture3D:
    return 3;
  case dxil::ResourceKind::TextureCube:
    return 3;
  case dxil::ResourceKind::TextureCubeArray:
    return 4;
  default:
    return 0;
  }
}

/// The number of texel-offset components DXIL's `Sample`/`SampleLevel` ops
/// accept for \p Dim, or 0 if \p Dim allows none at all (DXIL disallows an
/// offset when sampling a cube map, since there is no well-defined adjacent
/// face direction -- see the `Sample`/`SampleLevel` intrinsic reference).
unsigned getTextureOffsetComponents(dxil::ResourceKind Dim) {
  switch (Dim) {
  case dxil::ResourceKind::Texture1D:
  case dxil::ResourceKind::Texture1DArray:
    return 1;
  case dxil::ResourceKind::Texture2D:
  case dxil::ResourceKind::Texture2DArray:
    return 2;
  case dxil::ResourceKind::Texture3D:
    return 3;
  default:
    return 0; // Cube, CubeArray: no offset operand.
  }
}

/// Packs \p Components (already-extracted scalar operands) into the fixed
/// vector `int_dx_resource_sample*`/`load_level`'s coord/offset operand
/// expects, or returns the lone scalar unwrapped for a single-component
/// dimension (`Texture1D`'s coordinate, matching how `TypedBufferExtType`'s
/// scalar-vs-vector element type distinction already works). Returns
/// nullptr for an empty \p Components (a dimension with no offset operand
/// at all, e.g. `TextureCube`).
Value *packTextureVector(IRBuilder<> &Builder, ArrayRef<Value *> Components) {
  if (Components.empty())
    return nullptr;
  if (Components.size() == 1)
    return Components[0];
  Value *Vec = PoisonValue::get(
      FixedVectorType::get(Components[0]->getType(), Components.size()));
  for (unsigned I = 0; I != Components.size(); ++I)
    Vec = Builder.CreateInsertElement(Vec, Components[I], Builder.getInt32(I));
  return Vec;
}

/// Rewrites every `extractvalue` reading a raised sample/load's `%dx.types.
/// ResRet` result -- exactly `replaceLoadResultUses`'s DXIL-import-side
/// counterpart in `feme/lib/Transforms/CPU/ResourceLowering.cpp`, but for
/// the raising direction: DXIL's `ResRet` struct always carries 4 value
/// fields plus a trailing status field regardless of the texture's real
/// texel width, so only extracts up to \p Width (the handle's actual
/// component count) are recognized; anything else (including any read of
/// the status field, which the raised intrinsic has no equivalent for) is
/// left unraised, exactly as `raiseTypedBufferLoad` already does for
/// buffers. Returns false leaving \p CI untouched if any use doesn't match.
bool replaceResRetExtracts(CallInst &CI, unsigned Width, Value *Loaded,
                           IRBuilder<> &Builder) {
  SmallVector<ExtractValueInst *, 4> Extracts;
  for (User *U : CI.users()) {
    auto *EV = dyn_cast<ExtractValueInst>(U);
    if (!EV || EV->getNumIndices() != 1 || EV->getIndices()[0] >= Width)
      return false;
    Extracts.push_back(EV);
  }

  auto *VecTy = dyn_cast<FixedVectorType>(Loaded->getType());
  for (ExtractValueInst *EV : Extracts) {
    Builder.SetInsertPoint(EV);
    Value *Component =
        VecTy ? Builder.CreateExtractElement(Loaded, EV->getIndices()[0])
              : Loaded;
    EV->replaceAllUsesWith(Component);
    EV->eraseFromParent();
  }
  return true;
}

/// Raises a `dx.op.sample` (opcode 60, implicit-LOD sample) call on an
/// already-raised `dx.Texture` handle and `dx.Sampler` handle into
/// `llvm.dx.resource.sample`, reassembling the split `Coord0..3`/`Offset0..2`
/// scalar operands back into the fixed vectors the canonical intrinsic
/// expects (see `lowerSampleOp` in
/// `llvm/lib/Target/DirectX/DXILOpLowering.cpp`, the exact forward direction
/// this inverts), keyed by the handle's dimensionality for how many of each
/// are meaningful. `Clamp` (the trailing operand) must be `undef`/`poison`:
/// a real clamp value means the op was actually lowered from
/// `int_dx_resource_sample_clamp`, a separate call this pass does not (yet)
/// raise to.
bool raiseSample(CallInst &CI) {
  if (CI.arg_size() !=
      11) // opcode, Handle, Sampler, Coord0-3, Offset0-2, Clamp
    return false;
  Value *Handle = lookThroughCastHandle(CI.getArgOperand(1));
  auto *HandleTy =
      Handle ? dyn_cast<dxil::TextureExtType>(Handle->getType()) : nullptr;
  Value *Sampler = lookThroughCastHandle(CI.getArgOperand(2));
  if (!HandleTy || !Sampler || !isa<dxil::SamplerExtType>(Sampler->getType()))
    return false;
  Value *Clamp = CI.getArgOperand(10);
  if (!isa<UndefValue>(Clamp))
    return false;

  unsigned NumCoords = getTextureCoordComponents(HandleTy->getDimension());
  if (NumCoords == 0)
    return false;
  unsigned NumOffsets = getTextureOffsetComponents(HandleTy->getDimension());

  IRBuilder<> Builder(&CI);
  SmallVector<Value *, 4> Coords;
  for (unsigned I = 0; I != NumCoords; ++I)
    Coords.push_back(CI.getArgOperand(3 + I));
  SmallVector<Value *, 3> Offsets;
  for (unsigned I = 0; I != NumOffsets; ++I)
    Offsets.push_back(CI.getArgOperand(7 + I));

  Value *Coord = packTextureVector(Builder, Coords);
  Value *Offset = packTextureVector(Builder, Offsets);
  if (!Offset)
    Offset = PoisonValue::get(Type::getInt32Ty(CI.getContext()));

  Type *ElemTy = HandleTy->getResourceType();
  unsigned Width = isa<FixedVectorType>(ElemTy)
                       ? cast<FixedVectorType>(ElemTy)->getNumElements()
                       : 1;
  Function *SampleFn = Intrinsic::getOrInsertDeclaration(
      CI.getModule(), Intrinsic::dx_resource_sample,
      {ElemTy, HandleTy, Sampler->getType(), Coord->getType(),
       Offset->getType()});
  Value *Loaded =
      Builder.CreateCall(SampleFn, {Handle, Sampler, Coord, Offset});
  if (!replaceResRetExtracts(CI, Width, Loaded, Builder))
    return false;
  CI.eraseFromParent();
  return true;
}

/// Raises a `dx.op.sampleLevel` (opcode 62, explicit-LOD sample) call into
/// `llvm.dx.resource.samplelevel`, the same coordinate/offset reassembly as
/// `raiseSample` plus the explicit LOD operand (`SampleLevel` has no
/// `Clamp` operand at all -- see DXIL.td).
bool raiseSampleLevel(CallInst &CI) {
  if (CI.arg_size() != 11) // opcode, Handle, Sampler, Coord0-3, Offset0-2, LOD
    return false;
  Value *Handle = lookThroughCastHandle(CI.getArgOperand(1));
  auto *HandleTy =
      Handle ? dyn_cast<dxil::TextureExtType>(Handle->getType()) : nullptr;
  Value *Sampler = lookThroughCastHandle(CI.getArgOperand(2));
  if (!HandleTy || !Sampler || !isa<dxil::SamplerExtType>(Sampler->getType()))
    return false;

  unsigned NumCoords = getTextureCoordComponents(HandleTy->getDimension());
  if (NumCoords == 0)
    return false;
  unsigned NumOffsets = getTextureOffsetComponents(HandleTy->getDimension());

  IRBuilder<> Builder(&CI);
  SmallVector<Value *, 4> Coords;
  for (unsigned I = 0; I != NumCoords; ++I)
    Coords.push_back(CI.getArgOperand(3 + I));
  SmallVector<Value *, 3> Offsets;
  for (unsigned I = 0; I != NumOffsets; ++I)
    Offsets.push_back(CI.getArgOperand(7 + I));

  Value *Coord = packTextureVector(Builder, Coords);
  Value *Offset = packTextureVector(Builder, Offsets);
  if (!Offset)
    Offset = PoisonValue::get(Type::getInt32Ty(CI.getContext()));
  Value *Lod = CI.getArgOperand(10);

  Type *ElemTy = HandleTy->getResourceType();
  unsigned Width = isa<FixedVectorType>(ElemTy)
                       ? cast<FixedVectorType>(ElemTy)->getNumElements()
                       : 1;
  Function *SampleLevelFn = Intrinsic::getOrInsertDeclaration(
      CI.getModule(), Intrinsic::dx_resource_samplelevel,
      {ElemTy, HandleTy, Sampler->getType(), Coord->getType(),
       Offset->getType()});
  Value *Loaded =
      Builder.CreateCall(SampleLevelFn, {Handle, Sampler, Coord, Lod, Offset});
  if (!replaceResRetExtracts(CI, Width, Loaded, Builder))
    return false;
  CI.eraseFromParent();
  return true;
}

/// Raises a `dx.op.textureLoad` (opcode 66) call on an already-raised
/// `dx.Texture` handle into `llvm.dx.resource.load.level`: an explicit-mip,
/// no-sampler texel fetch (see `lowerTextureLoad` in DXILOpLowering.cpp, the
/// forward direction this inverts). Coordinates are integer, unlike
/// `Sample`'s floating-point ones, so no separate offset-component helper is
/// needed -- `TextureLoad`'s own 3-wide `Coord0..2`/`Offset0..2` operand
/// pairs are simply truncated to \p Dim's component count.
bool raiseTextureLoad(CallInst &CI) {
  if (CI.arg_size() != 9) // opcode, Handle, MipLevel, Coord0-2, Offset0-2
    return false;
  Value *Handle = lookThroughCastHandle(CI.getArgOperand(1));
  auto *HandleTy =
      Handle ? dyn_cast<dxil::TextureExtType>(Handle->getType()) : nullptr;
  if (!HandleTy)
    return false;

  unsigned NumCoords = getTextureCoordComponents(HandleTy->getDimension());
  if (NumCoords == 0 || NumCoords > 3)
    return false;
  unsigned NumOffsets = getTextureOffsetComponents(HandleTy->getDimension());

  IRBuilder<> Builder(&CI);
  Value *MipLevel = CI.getArgOperand(2);
  SmallVector<Value *, 3> Coords;
  for (unsigned I = 0; I != NumCoords; ++I)
    Coords.push_back(CI.getArgOperand(3 + I));
  SmallVector<Value *, 3> Offsets;
  for (unsigned I = 0; I != NumOffsets; ++I)
    Offsets.push_back(CI.getArgOperand(6 + I));

  Value *Coord = packTextureVector(Builder, Coords);
  Value *Offset = packTextureVector(Builder, Offsets);
  if (!Offset)
    Offset = PoisonValue::get(Type::getInt32Ty(CI.getContext()));

  Type *ElemTy = HandleTy->getResourceType();
  unsigned Width = isa<FixedVectorType>(ElemTy)
                       ? cast<FixedVectorType>(ElemTy)->getNumElements()
                       : 1;
  Function *LoadLevelFn = Intrinsic::getOrInsertDeclaration(
      CI.getModule(), Intrinsic::dx_resource_load_level,
      {ElemTy, HandleTy, Coord->getType(), MipLevel->getType(),
       Offset->getType()});
  Value *Loaded =
      Builder.CreateCall(LoadLevelFn, {Handle, Coord, MipLevel, Offset});
  if (!replaceResRetExtracts(CI, Width, Loaded, Builder))
    return false;
  CI.eraseFromParent();
  return true;
}

/// Raises a `dx.op.textureStore` (opcode 67) call on an already-raised
/// `dx.Texture` handle into `llvm.dx.resource.store.texture`, the write
/// counterpart `raiseTextureLoad` inverts the read side of. `TextureStore`'s
/// `Coord0..2` operands are simply truncated to \p Dim's component count
/// (as `raiseTextureLoad` does for its own `Coord0..2`); its four scalar
/// `Val0..3` operands are reassembled into the handle's element (vector)
/// type the same way `raiseTypedBufferStore` reassembles `BufferStore`'s.
/// Only a full-width write (a write mask selecting every one of the
/// element's components, e.g. `0b1111` for a `<4 x T>`) is raised, matching
/// `raiseTypedBufferStore`'s identical narrowing -- a partial-component
/// texture write is left unraised for now.
bool raiseTextureStore(CallInst &CI) {
  if (CI.arg_size() != 10) // opcode, Handle, Coord0-2, Val0-3, Mask
    return false;
  Value *Handle = lookThroughCastHandle(CI.getArgOperand(1));
  auto *HandleTy =
      Handle ? dyn_cast<dxil::TextureExtType>(Handle->getType()) : nullptr;
  if (!HandleTy)
    return false;

  unsigned NumCoords = getTextureCoordComponents(HandleTy->getDimension());
  if (NumCoords == 0 || NumCoords > 3)
    return false;

  std::optional<uint64_t> Mask = getConstInt(CI.getArgOperand(9));
  if (!Mask)
    return false;
  unsigned Width = getContiguousMaskWidth(*Mask);
  Type *ElemTy = HandleTy->getResourceType();
  auto *VecTy = dyn_cast<FixedVectorType>(ElemTy);
  if (Width != (VecTy ? VecTy->getNumElements() : 1))
    return false;

  IRBuilder<> Builder(&CI);
  SmallVector<Value *, 3> Coords;
  for (unsigned I = 0; I != NumCoords; ++I)
    Coords.push_back(CI.getArgOperand(2 + I));
  Value *Coord = packTextureVector(Builder, Coords);

  Value *Stored = CI.getArgOperand(5);
  if (VecTy) {
    Stored = PoisonValue::get(VecTy);
    for (unsigned I = 0; I != Width; ++I)
      Stored = Builder.CreateInsertElement(Stored, CI.getArgOperand(5 + I),
                                           Builder.getInt32(I));
  }
  if (Stored->getType() != ElemTy)
    return false;

  Function *StoreFn = Intrinsic::getOrInsertDeclaration(
      CI.getModule(), Intrinsic::dx_resource_store_texture,
      {HandleTy, Coord->getType(), ElemTy});
  Builder.CreateCall(StoreFn, {Handle, Coord, Stored});
  CI.eraseFromParent();
  return true;
}

/// Raises a `dx.op.getDimensions` (opcode 72) call's field-0 (`.x`, i.e.
/// width) extract into `llvm.dx.resource.getdimensions_x`, the only overload
/// LLVM's own `DXILOpLowering.cpp` lowers a canonical intrinsic to yet (see
/// this section's header comment): any other field, or a use that isn't a
/// field-0 `extractvalue`, is left unraised.
bool raiseGetDimensionsX(CallInst &CI) {
  if (CI.arg_size() != 3) // opcode, Handle, MipLevel
    return false;
  Value *Handle = lookThroughCastHandle(CI.getArgOperand(1));
  if (!Handle ||
      !isa<dxil::TextureExtType, dxil::MSTextureExtType>(Handle->getType()))
    return false;

  SmallVector<ExtractValueInst *, 1> Extracts;
  for (User *U : CI.users()) {
    auto *EV = dyn_cast<ExtractValueInst>(U);
    if (!EV || EV->getNumIndices() != 1 || EV->getIndices()[0] != 0)
      return false;
    Extracts.push_back(EV);
  }
  if (Extracts.empty())
    return false;

  IRBuilder<> Builder(&CI);
  Function *GetDimensionsXFn = Intrinsic::getOrInsertDeclaration(
      CI.getModule(), Intrinsic::dx_resource_getdimensions_x,
      {Handle->getType()});
  Value *Width = Builder.CreateCall(GetDimensionsXFn, {Handle});
  for (ExtractValueInst *EV : Extracts) {
    EV->replaceAllUsesWith(Width);
    EV->eraseFromParent();
  }
  CI.eraseFromParent();
  return true;
}

/// Returns the `llvm.dx.resource.load.cbufferrow.*` intrinsic whose row
/// shape matches \p RetTy (`%dx.types.CBufRet.*`, a fixed-width aggregate of
/// same-typed fields covering DXIL's 128-bit cbuffer row -- see
/// `CBufferLoadLegacy`'s `overloads` list in DXIL.td: 2 64-bit fields, 4
/// 32-bit fields, or 8 16-bit fields), or `not_intrinsic` if \p RetTy is not
/// one of those three shapes.
Intrinsic::ID getCBufferRowIntrinsic(StructType &RetTy) {
  Type *ElemTy = RetTy.getElementType(0);
  for (Type *Field : RetTy.elements())
    if (Field != ElemTy)
      return Intrinsic::not_intrinsic; // Every field must share one type.

  switch (RetTy.getNumElements()) {
  case 2:
    return ElemTy->isIntegerTy(64) || ElemTy->isDoubleTy()
               ? Intrinsic::dx_resource_load_cbufferrow_2
               : Intrinsic::not_intrinsic;
  case 4:
    return ElemTy->isIntegerTy(32) || ElemTy->isFloatTy()
               ? Intrinsic::dx_resource_load_cbufferrow_4
               : Intrinsic::not_intrinsic;
  case 8:
    return ElemTy->isIntegerTy(16) || ElemTy->isHalfTy()
               ? Intrinsic::dx_resource_load_cbufferrow_8
               : Intrinsic::not_intrinsic;
  default:
    return Intrinsic::not_intrinsic;
  }
}

/// Raises a `dx.op.cbufferLoadLegacy` (opcode 59) call on an already-raised
/// constant-buffer handle into the `llvm.dx.resource.load.cbufferrow.*`
/// overload `getCBufferRowIntrinsic` selects for its return shape: the
/// 32-bit-per-component row (`.f32`/`.i32`, `cbufferrow.4`), the 16-bit one
/// HLSL's `half`/16-bit-int cbuffer members produce with
/// `-enable-16bit-types` (`.f16`/`.i16`, `cbufferrow.8`), or the 64-bit one
/// `double`/64-bit-int members produce (`.f64`/`.i64`, `cbufferrow.2`).
bool raiseCBufferLoadLegacy(CallInst &CI) {
  if (CI.arg_size() != 3)
    return false;
  Value *Handle = lookThroughCastHandle(CI.getArgOperand(1));
  auto *HandleTy =
      Handle ? dyn_cast<TargetExtType>(Handle->getType()) : nullptr;
  if (!HandleTy || HandleTy->getName() != "dx.CBuffer")
    return false;

  auto *RetTy = dyn_cast<StructType>(CI.getType());
  if (!RetTy)
    return false;
  Intrinsic::ID LoadID = getCBufferRowIntrinsic(*RetTy);
  if (LoadID == Intrinsic::not_intrinsic)
    return false;
  Type *ElemTy = RetTy->getElementType(0);
  unsigned NumFields = RetTy->getNumElements();

  // `CI`'s result type (DXIL's named `%dx.types.CBufRet.*`) and the
  // intrinsic's (a literal struct) are layout- but never `llvm::Type`-
  // identical (see `raiseAggregateCall`'s comment for the same point about
  // `WaveActiveBallot` et al.), so each `extractvalue` of `CI` is rewritten
  // individually to read the new call instead of RAUWing the aggregate
  // value itself.
  SmallVector<ExtractValueInst *, 8> Extracts;
  for (User *U : CI.users()) {
    auto *EV = dyn_cast<ExtractValueInst>(U);
    if (!EV || EV->getNumIndices() != 1 || EV->getIndices()[0] >= NumFields)
      return false;
    Extracts.push_back(EV);
  }

  IRBuilder<> Builder(&CI);
  SmallVector<Type *, 9> Overloads(NumFields, ElemTy);
  Overloads.push_back(HandleTy);
  Function *LoadFn =
      Intrinsic::getOrInsertDeclaration(CI.getModule(), LoadID, Overloads);
  Value *Loaded = Builder.CreateCall(LoadFn, {Handle, CI.getArgOperand(2)});

  for (ExtractValueInst *EV : Extracts) {
    Builder.SetInsertPoint(EV);
    Value *Field = Builder.CreateExtractValue(Loaded, EV->getIndices()[0]);
    EV->replaceAllUsesWith(Field);
    EV->eraseFromParent();
  }
  CI.eraseFromParent();
  return true;
}

/// Raises a `dx.op.rawBufferStore` (opcode 140) call on an already-raised
/// raw/structured buffer handle into `llvm.dx.resource.store.rawbuffer`.
/// `RawBufferStore`'s `Coord0`/`Coord1` operands carry whichever of "byte
/// offset" (a `ByteAddressBuffer`) or "element index, byte offset within
/// element" (a `StructuredBuffer`) the handle's resource kind means -- both
/// forward straight through to the raised intrinsic's own `Coord0`/`Coord1`
/// operands unexamined, so this needs no resource-kind-specific handling of
/// its own (see `int_dx_resource_store_rawbuffer`'s own operand comment).
/// Only a single-component (mask 0b0001) store is raised, matching what
/// `libFeMeRuntimeCPU`'s raw/structured buffer view implements today (see
/// the "Descriptor formats" section of feme/docs/FeMeCPUDesign.md); a
/// multi-component store is left unraised for now.
bool raiseRawBufferStore(CallInst &CI) {
  if (CI.arg_size() != 10)
    return false;
  Value *Handle = lookThroughCastHandle(CI.getArgOperand(1));
  auto *HandleTy =
      Handle ? dyn_cast<TargetExtType>(Handle->getType()) : nullptr;
  if (!HandleTy || HandleTy->getName() != "dx.RawBuffer")
    return false;

  std::optional<uint64_t> Mask = getConstInt(CI.getArgOperand(8));
  if (!Mask || *Mask != 1)
    return false;

  Value *Coord0 = CI.getArgOperand(2);
  Value *Coord1 = CI.getArgOperand(3);
  Value *Stored = CI.getArgOperand(4);

  IRBuilder<> Builder(&CI);
  Function *StoreFn = Intrinsic::getOrInsertDeclaration(
      CI.getModule(), Intrinsic::dx_resource_store_rawbuffer,
      {HandleTy, Stored->getType()});
  Builder.CreateCall(StoreFn, {Handle, Coord0, Coord1, Stored});
  CI.eraseFromParent();
  return true;
}

/// Raises a `dx.op.rawBufferLoad` (opcode 139) call on an already-raised
/// raw/structured buffer handle into `llvm.dx.resource.load.rawbuffer`,
/// mirroring `raiseTypedBufferLoad`: the loaded scalar's type is recovered
/// from the `extractvalue`s reading the DXIL `%dx.types.ResRet` return
/// struct's component 0 (the only component a single-component load ever
/// reads), and a load consumed any other way (multiple components, or
/// `ResRet`'s trailing status field) is left unraised.
bool raiseRawBufferLoad(CallInst &CI) {
  if (CI.arg_size() != 6)
    return false;
  Value *Handle = lookThroughCastHandle(CI.getArgOperand(1));
  auto *HandleTy =
      Handle ? dyn_cast<TargetExtType>(Handle->getType()) : nullptr;
  if (!HandleTy || HandleTy->getName() != "dx.RawBuffer")
    return false;

  std::optional<uint64_t> Mask = getConstInt(CI.getArgOperand(4));
  if (!Mask || *Mask != 1)
    return false;

  SmallVector<ExtractValueInst *, 2> Extracts;
  for (User *U : CI.users()) {
    auto *EV = dyn_cast<ExtractValueInst>(U);
    if (!EV || EV->getNumIndices() != 1 || EV->getIndices()[0] != 0)
      return false;
    Extracts.push_back(EV);
  }
  if (Extracts.empty())
    return false;

  IRBuilder<> Builder(&CI);
  Function *LoadFn = Intrinsic::getOrInsertDeclaration(
      CI.getModule(), Intrinsic::dx_resource_load_rawbuffer,
      {Extracts[0]->getType(), HandleTy});
  Value *Loaded = Builder.CreateCall(
      LoadFn, {Handle, CI.getArgOperand(2), CI.getArgOperand(3)});
  Value *Value0 = Builder.CreateExtractValue(Loaded, 0);

  for (ExtractValueInst *EV : Extracts) {
    EV->replaceAllUsesWith(Value0);
    EV->eraseFromParent();
  }
  CI.eraseFromParent();
  return true;
}

/// Runs the resource-op raising phases in dependency order over \p M: handle
/// creation first (so that the accesses below have a `target("dx.")` handle
/// to consume), then the buffer accesses that consume those handles, then
/// cleanup of the `llvm.dx.resource.casthandle` bridges left unused once
/// every consumer of a handle has been raised.
bool raiseResourceOps(Module &M) {
  bool Changed = false;
  ResourceMetadata MD = ResourceMetadata::read(M);

  auto forEachDXOpCall = [&M](unsigned Opcode, auto Raise) {
    bool Changed = false;
    for (Function &F : llvm::make_early_inc_range(M.functions())) {
      if (!F.isDeclaration() || !F.getName().starts_with("dx.op."))
        continue;
      for (User *U : llvm::make_early_inc_range(F.users())) {
        auto *CI = dyn_cast<CallInst>(U);
        if (!CI || CI->getCalledFunction() != &F || CI->arg_size() == 0)
          continue;
        if (getConstInt(CI->getArgOperand(0)) != Opcode)
          continue;
        Changed |= Raise(*CI);
      }
    }
    return Changed;
  };

  Changed |= forEachDXOpCall(216, [](CallInst &CI) { // AnnotateHandle
    // A handle bridged from either CreateHandleFromBinding (register-bound)
    // or CreateHandleFromHeap (bindless); each raiser recognizes its own
    // handle operand's callee name and declines otherwise (see each
    // function's comment).
    if (raiseResourceHandleFromBinding(CI))
      return true;
    return raiseResourceHandleFromHeap(CI);
  });
  Changed |= forEachDXOpCall(57, [&MD](CallInst &CI) { // CreateHandle
    return raiseLegacyCreateHandle(CI, MD);
  });
  Changed |= forEachDXOpCall(69, [](CallInst &CI) { // BufferStore
    return raiseTypedBufferStore(CI);
  });
  Changed |= forEachDXOpCall(68, [](CallInst &CI) { // BufferLoad
    return raiseTypedBufferLoad(CI);
  });
  Changed |= forEachDXOpCall(140, [](CallInst &CI) { // RawBufferStore
    return raiseRawBufferStore(CI);
  });
  Changed |= forEachDXOpCall(139, [](CallInst &CI) { // RawBufferLoad
    return raiseRawBufferLoad(CI);
  });
  Changed |= forEachDXOpCall(59, [](CallInst &CI) { // CBufferLoadLegacy
    return raiseCBufferLoadLegacy(CI);
  });
  Changed |= forEachDXOpCall(60, [](CallInst &CI) { // Sample
    return raiseSample(CI);
  });
  Changed |= forEachDXOpCall(62, [](CallInst &CI) { // SampleLevel
    return raiseSampleLevel(CI);
  });
  Changed |= forEachDXOpCall(66, [](CallInst &CI) { // TextureLoad
    return raiseTextureLoad(CI);
  });
  Changed |= forEachDXOpCall(67, [](CallInst &CI) { // TextureStore
    return raiseTextureStore(CI);
  });
  Changed |= forEachDXOpCall(72, [](CallInst &CI) { // GetDimensions
    return raiseGetDimensionsX(CI);
  });

  for (Function &F : llvm::make_early_inc_range(M.functions())) {
    if (F.getIntrinsicID() != Intrinsic::dx_resource_casthandle)
      continue;
    for (User *U : llvm::make_early_inc_range(F.users()))
      if (auto *CI = dyn_cast<CallInst>(U); CI && CI->use_empty()) {
        CI->eraseFromParent();
        Changed = true;
      }
    if (F.use_empty())
      F.eraseFromParent();
  }

  return Changed;
}

} // namespace

PreservedAnalyses OpRaisingPass::run(Module &M, ModuleAnalysisManager &AM) {
  bool Changed = raiseResourceOps(M);

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

      if (Opcode == 80) { // Barrier
        Changed |= raiseBarrierCall(*CI);
        continue;
      }

      if (Opcode == 119 || Opcode == 121) { // WaveActiveOp, WavePrefixOp
        Changed |= raiseReduceOpCall(*CI, Opcode);
        continue;
      }

      if (Opcode == 120) { // WaveActiveBit
        Changed |= raiseWaveActiveBitCall(*CI);
        continue;
      }

      if (Opcode == 123) { // QuadOp
        Changed |= raiseQuadOpCall(*CI);
        continue;
      }

      if (const RaisableAggregateOp *RaiseAsAggregate =
              lookupRaisableAggregateOp(Opcode)) {
        Changed |= raiseAggregateCall(*CI, *RaiseAsAggregate);
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
