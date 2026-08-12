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
// uniformly). Opcodes intentionally NOT covered here (documented in
// feme/docs/Design.md) either:
//  - return an aggregate/multiple values that would need `extractvalue`
//    reconstruction (`IMul`/`UMul`, `UAddc`, `SplitDouble`,
//    `WaveActiveBallot`),
//  - pick their source intrinsic based on an extra "kind"/flag operand
//    rather than the opcode alone (`WaveActiveOp`, `WaveActiveBit`,
//    `WavePrefixOp`, `QuadOp`) -- `Barrier`'s mode flags are the same shape
//    of problem, but it is now covered by `raiseBarrierCall`/
//    `RaisableBarriers` below instead, since the CPU target requires it
//    (see feme/docs/FeMeCPUDesign.md's "Raised IR prerequisites"), or
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

/// Raises a `dx.op.annotateHandle` (opcode 216) call whose handle operand is
/// a `dx.op.createHandleFromBinding` (opcode 217) call back into a single
/// `llvm.dx.resource.handlefrombinding` intrinsic call, reconstructing the
/// resource's `target("dx.")` handle type from the two ops' constant
/// `%dx.types.ResBind`/`%dx.types.ResourceProperties` struct operands -- the
/// `llvm::hlsl`-style resource metadata reconstruction called out as future
/// work in an earlier version of this pass (see feme/docs/Design.md).
///
/// `TypedBuffer` and unstructured `RawBuffer` (`ByteAddressBuffer`) element
/// types are recovered exactly, since their full shape (a scalar, or
/// nothing) is present in `ResourceProperties`. `StructuredBuffer`/`CBuffer`
/// only have their element/layout struct's size (and, for
/// `StructuredBuffer`, alignment) recoverable, not its original field
/// layout -- raising those still matters for re-targeting the IR that
/// consumes the handle (its binding, and the byte size buffer indexing
/// depends on, are exactly reconstructed), so this raises them too, via an
/// opaque size-only placeholder element type (`getOpaqueSizedType` above)
/// rather than leaving them as unrecognized `dx.op.*` calls. Textures and
/// samplers still need dimension/multi-sample/feedback information this
/// pass doesn't yet decode, so those remain unraised for now (see
/// feme/docs/Design.md). Since the buffer/texture load and store ops that
/// would actually consume this handle aren't raised yet either, the
/// reconstructed handle is bridged back to the legacy `%dx.types.Handle`
/// type via `llvm.dx.resource.casthandle` -- the same "temporary" cast
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

  // See ResourceInfo::getAnnotateProps (llvm/lib/Analysis/DXILResource.cpp)
  // for this bit layout -- it's the exact forward direction this inverts.
  auto Kind = static_cast<dxil::ResourceKind>(*Word0 & 0xFF);
  bool IsUAV = (*Word0 >> 12) & 1;
  bool IsROV = (*Word0 >> 13) & 1;

  LLVMContext &Ctx = AnnotateCI.getContext();
  TargetExtType *HandleTy = nullptr;
  if (Kind == dxil::ResourceKind::TypedBuffer) {
    auto ElemKind = static_cast<dxil::ElementType>(*Word1 & 0xFF);
    Type *ElemTy = getElementLLVMType(ElemKind, Ctx);
    if (!ElemTy)
      return false;
    HandleTy = TargetExtType::get(
        Ctx, "dx.TypedBuffer", {ElemTy},
        {static_cast<unsigned>(IsUAV), static_cast<unsigned>(IsROV),
         static_cast<unsigned>(isSignedElementType(ElemKind))});
  } else if (Kind == dxil::ResourceKind::RawBuffer && *Word1 == 0) {
    HandleTy = TargetExtType::get(
        Ctx, "dx.RawBuffer", {Type::getInt8Ty(Ctx)},
        {static_cast<unsigned>(IsUAV), static_cast<unsigned>(IsROV)});
  } else if (Kind == dxil::ResourceKind::StructuredBuffer) {
    // Word0's AlignLog2 field (bits 8-11) is only populated for structured
    // buffers (see `ResourceInfo::getAnnotateProps`), and Word1 is the
    // element stride in bytes for this kind.
    uint32_t AlignLog2 = (*Word0 >> 8) & 0xF;
    Type *ElemTy =
        getOpaqueSizedType(Ctx, static_cast<uint32_t>(*Word1), AlignLog2);
    HandleTy = TargetExtType::get(
        Ctx, "dx.RawBuffer", {ElemTy},
        {static_cast<unsigned>(IsUAV), static_cast<unsigned>(IsROV)});
  } else if (Kind == dxil::ResourceKind::CBuffer) {
    // CBuffer's ResourceProperties encoding carries no alignment bits at
    // all (`AlignLog2` is only ever set for `StructuredBuffer`), so there's
    // nothing to recover beyond size here.
    Type *ElemTy = getOpaqueSizedType(Ctx, static_cast<uint32_t>(*Word1), 0);
    HandleTy = TargetExtType::get(Ctx, "dx.CBuffer", {ElemTy});
  } else {
    return false; // Unsupported resource kind: leave both ops unmodified.
  }

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

/// Infers the number of components in a typed buffer's element type from the
/// `dx.op.bufferStore`/`dx.op.bufferLoad` calls consuming \p HandleCI's
/// result. DXIL's metadata only records a typed buffer's *component* type
/// (`float`), never its vector width (`<4 x float>`), so the width has to be
/// recovered from how the resource is actually accessed: a store's write mask
/// names it directly, and a load's `%dx.types.ResRet` components are only
/// extracted up to it. Defaults to 4 (DXIL's widest typed buffer element)
/// when the handle has no accesses to learn from.
unsigned inferTypedBufferWidth(const CallInst &HandleCI) {
  unsigned Width = 0;
  for (const User *U : HandleCI.users()) {
    const auto *CI = dyn_cast<CallInst>(U);
    const Function *Callee = CI ? CI->getCalledFunction() : nullptr;
    if (!Callee)
      continue;

    if (Callee->getName().starts_with("dx.op.bufferStore") &&
        CI->arg_size() == 9) {
      if (std::optional<uint64_t> Mask = getConstInt(CI->getArgOperand(8)))
        Width = std::max(Width, getContiguousMaskWidth(*Mask));
      continue;
    }

    if (!Callee->getName().starts_with("dx.op.bufferLoad"))
      continue;
    for (const User *LoadUser : CI->users())
      if (const auto *EV = dyn_cast<ExtractValueInst>(LoadUser))
        if (EV->getNumIndices() == 1 && EV->getIndices()[0] < 4)
          Width = std::max(Width, EV->getIndices()[0] + 1);
  }
  return Width ? Width : 4;
}

/// Builds the `target("dx.")` handle type for \p Binding, or returns nullptr
/// for resource kinds this pass doesn't yet reconstruct (textures, samplers,
/// and the norm/packed typed buffer element formats -- see
/// `getElementLLVMType`). \p VectorWidth is the typed buffer element vector
/// width recovered by `inferTypedBufferWidth`.
TargetExtType *buildHandleType(LLVMContext &Ctx, const ResourceBinding &Binding,
                               unsigned VectorWidth) {
  bool IsUAV = Binding.Class == dxil::ResourceClass::UAV;
  switch (Binding.Kind) {
  case dxil::ResourceKind::TypedBuffer: {
    Type *ScalarTy = getElementLLVMType(Binding.ElementType, Ctx);
    if (!ScalarTy)
      return nullptr;
    Type *ElemTy = VectorWidth > 1
                       ? cast<Type>(FixedVectorType::get(ScalarTy, VectorWidth))
                       : ScalarTy;
    return TargetExtType::get(
        Ctx, "dx.TypedBuffer", {ElemTy},
        {static_cast<unsigned>(IsUAV), static_cast<unsigned>(Binding.IsROV),
         static_cast<unsigned>(isSignedElementType(Binding.ElementType))});
  }
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
    return raiseResourceHandleFromBinding(CI);
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
