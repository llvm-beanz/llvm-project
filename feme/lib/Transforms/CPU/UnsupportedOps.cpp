//===- UnsupportedOps.cpp - CPU target early raised-op diagnostics -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/UnsupportedOps.h"

#include "feme/Transforms/CPU/ResourceLowering.h"
#include "feme/Transforms/CPU/RootConstantLowering.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/DXILResource.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace feme::cpu {

Error checkSupportedRaisedOps(const Module &M) {
  if (Error E = validateRootConstantBindings(M))
    return E;
  // A `dx_resource_handlefrombinding` call this milestone's root-constant
  // support (`feme::cpu::RootConstantLoweringPass`/
  // `feme::cpu::ResourceLoweringPass`, see RootConstantLowering.h) accepts
  // is not an unsupported operation, even though it is still present at
  // this point (both passes run after this check -- see "Root constants"
  // in feme/docs/FeMeCPUDesign.md and each pass's own header comment for
  // why neither lowers it here itself). Collected up front, over every
  // function in `M`, since the handle being checked below belongs to
  // whichever function actually calls it, not the (module-level)
  // declaration this loop iterates.
  SmallPtrSet<const CallInst *, 4> AcceptedRootConstantHandles;
  for (const Function &F : M)
    if (std::optional<RootConstantAccess> Access =
            matchRootConstantAccess(const_cast<Function &>(F)))
      AcceptedRootConstantHandles.insert(Access->Handle);

  for (const Function &F : M) {
    if (!F.isDeclaration() || F.use_empty())
      continue;

    // A leftover DXIL-specific op the front end didn't raise: the CPU
    // pipeline only understands the format-agnostic `llvm.{dx,spv}.*`
    // vocabulary (see "Raised IR prerequisites").
    if (F.getName().starts_with("dx.op."))
      return createStringError(
          inconvertibleErrorCode(),
          "unsupported raised operation: '%s' was not raised to idiomatic "
          "LLVM IR before reaching the FeMe CPU target",
          F.getName().str().c_str());

    switch (F.getIntrinsicID()) {
    case Intrinsic::dx_resource_getdimensions_x:
    case Intrinsic::dx_resource_getdimensions_xy:
    case Intrinsic::dx_resource_getdimensions_xyz:
    case Intrinsic::dx_resource_getdimensions_levels_x:
    case Intrinsic::dx_resource_getdimensions_levels_xy:
    case Intrinsic::dx_resource_getdimensions_levels_xyz:
    case Intrinsic::dx_resource_getdimensions_samples_xy:
    case Intrinsic::dx_resource_getdimensions_samples_xyz:
      for (const User *U : F.users()) {
        const auto *CI = dyn_cast<CallInst>(U);
        if ((F.getIntrinsicID() == Intrinsic::dx_resource_getdimensions_x ||
             F.getIntrinsicID() == Intrinsic::dx_resource_getdimensions_xy) &&
            CI &&
            isa<dxil::TypedBufferExtType, dxil::RawBufferExtType>(
                CI->getArgOperand(0)->getType()))
          continue;
        if (!CI || !isSupportedDXILGetDimensions(*CI))
          return createStringError(
              inconvertibleErrorCode(),
              "unsupported raised operation: '%s' requires heap-backed "
              "FP32/i32 1D/array or plain 2D mip-zero texture dimension "
              "queries, sampled cube dimensions, or sampled 1D/array or "
              "plain 2D explicit-mip/count queries, or 2D-array base/sampled "
              "explicit-mip/count queries, or 3D base/sampled "
              "explicit-mip/count queries, sampled cube-array dimensions, "
              "or sampled 2DMS/array dimensions/sample counts",
              F.getName().str().c_str());
      }
      continue;
    case Intrinsic::dx_resource_load_ms:
      for (const User *U : F.users()) {
        const auto *CI = dyn_cast<CallInst>(U);
        if (!CI || !isSupportedDXILLoad(*CI))
          return createStringError(
              inconvertibleErrorCode(),
              "unsupported raised operation: '%s' requires heap-backed "
              "FP32/i32 2DMS/array texel loading with paired/three-component "
              "i32 coordinates, an i32 sample index and constant signed "
              "XY offsets",
              F.getName().str().c_str());
      }
      continue;
    case Intrinsic::dx_resource_load_level:
      for (const User *U : F.users()) {
        const auto *CI = dyn_cast<CallInst>(U);
        const auto *Ty =
            CI ? dyn_cast<dxil::TextureExtType>(CI->getArgOperand(0)->getType())
               : nullptr;
        bool Is1D =
            Ty && (Ty->getDimension() == dxil::ResourceKind::Texture1D ||
                   Ty->getDimension() == dxil::ResourceKind::Texture1DArray);
        bool Is2D =
            Ty && (Ty->getDimension() == dxil::ResourceKind::Texture2D ||
                   Ty->getDimension() == dxil::ResourceKind::Texture2DArray);
        if (Ty &&
            (Is1D || Is2D ||
             Ty->getDimension() == dxil::ResourceKind::Texture3D) &&
            !isSupportedDXILLoad(*CI))
          return createStringError(
              inconvertibleErrorCode(),
              "unsupported raised operation: '%s' requires heap-backed "
              "FP32/i32 %s texel loading with %s i32 coordinates, an i32 "
              "mip and constant signed %s offsets",
              F.getName().str().c_str(),
              Is1D   ? "1D/array"
              : Is2D ? "2D/array"
                     : "3D",
              Is1D   ? "scalar/paired"
              : Is2D ? "paired/three-component"
                     : "three-component",
              Is1D   ? "scalar"
              : Is2D ? "XY"
                     : "XYZ");
      }
      continue;
    case Intrinsic::dx_resource_sample:
    case Intrinsic::dx_resource_samplelevel:
      for (const User *U : F.users()) {
        const auto *CI = dyn_cast<CallInst>(U);
        const auto *Ty =
            CI ? dyn_cast<dxil::TextureExtType>(CI->getArgOperand(0)->getType())
               : nullptr;
        bool Is2D =
            Ty && (Ty->getDimension() == dxil::ResourceKind::Texture2D ||
                   Ty->getDimension() == dxil::ResourceKind::Texture2DArray);
        bool IsCube =
            Ty && (Ty->getDimension() == dxil::ResourceKind::TextureCube ||
                   Ty->getDimension() == dxil::ResourceKind::TextureCubeArray);
        if (Ty &&
            (Ty->getDimension() == dxil::ResourceKind::Texture1D ||
             Ty->getDimension() == dxil::ResourceKind::Texture1DArray ||
             Ty->getDimension() == dxil::ResourceKind::Texture3D ||
             ((Is2D || IsCube) &&
              F.getIntrinsicID() == Intrinsic::dx_resource_samplelevel)) &&
            !(F.getIntrinsicID() == Intrinsic::dx_resource_sample
                  ? isSupportedDXILSample(*CI)
                  : isSupportedDXILSampleLevel(*CI)))
          return createStringError(
              inconvertibleErrorCode(),
              "unsupported raised operation: '%s' requires heap-backed FP32 "
              "%s %s sampling with a default sampler, "
              "shape-matched coordinates and %s offsets",
              F.getName().str().c_str(),
              IsCube ? "cube/array"
              : Is2D ? "2D/array"
              : Ty->getDimension() == dxil::ResourceKind::Texture3D
                  ? "3D"
                  : "1D/array",
              F.getIntrinsicID() == Intrinsic::dx_resource_sample
                  ? "fragment implicit"
                  : "explicit-LOD",
              IsCube ? "unused (zero/undef/poison) scalar"
              : Is2D ? "constant signed XY"
              : Ty->getDimension() == dxil::ResourceKind::Texture3D
                  ? "constant signed XYZ"
                  : "constant signed scalar");
      }
      continue;
    case Intrinsic::dx_resource_samplebias:
    case Intrinsic::dx_resource_samplebias_clamp:
      if (llvm::all_of(F.users(), [](const User *U) {
            const auto *CI = dyn_cast<CallInst>(U);
            return CI && isSupportedDXILSampleBias(*CI);
          }))
        continue;
      return createStringError(
          inconvertibleErrorCode(),
          "unsupported raised operation: '%s' requires heap-backed FP32 "
          "1D/2D/3D/cube (including array) fragment biased sampling with a "
          "default "
          "sampler, "
          "shape-matched coordinates and supported offsets; other "
          "stages/shapes are not yet supported",
          F.getName().str().c_str());
    case Intrinsic::dx_resource_samplegrad:
    case Intrinsic::dx_resource_samplegrad_clamp:
      if (llvm::all_of(F.users(), [](const User *U) {
            const auto *CI = dyn_cast<CallInst>(U);
            return CI && isSupportedDXILSampleGrad(*CI);
          }))
        continue;
      return createStringError(
          inconvertibleErrorCode(),
          "unsupported raised operation: '%s' requires heap-backed FP32 "
          "1D/2D/3D/cube (including array) explicit-gradient sampling with a "
          "default "
          "sampler, shape-matched coordinates/gradients and supported offsets "
          "(constant signed 1D/2D/3D or unused zero/undef/poison cube); "
          "other shapes are not yet supported",
          F.getName().str().c_str());
    case Intrinsic::dx_resource_samplecmp:
    case Intrinsic::dx_resource_samplecmp_clamp:
      if (llvm::all_of(F.users(), [](const User *U) {
            const auto *CI = dyn_cast<CallInst>(U);
            return CI && isSupportedDXILSampleCmp(*CI);
          }))
        continue;
      return createStringError(
          inconvertibleErrorCode(),
          "unsupported raised operation: '%s' requires heap-backed scalar FP32 "
          "1D/2D/cube (including array) fragment comparison sampling with "
          "shape-matched coordinates and supported offsets; derivative-aware "
          "lowering for "
          "other stages/shapes is not yet supported",
          F.getName().str().c_str());
    case Intrinsic::dx_resource_samplecmplevelzero:
      if (llvm::all_of(F.users(), [](const User *U) {
            const auto *CI = dyn_cast<CallInst>(U);
            return CI && isSupportedDXILSampleCmpLevelZero(*CI);
          }))
        continue;
      return createStringError(
          inconvertibleErrorCode(),
          "unsupported raised operation: '%s' requires scalar FP32 1D/2D/cube "
          "comparison sampling with heap handles, shape-matched coordinates "
          "and supported offsets",
          F.getName().str().c_str());
    case Intrinsic::dx_resource_handlefrombinding:
    case Intrinsic::dx_resource_handlefromimplicitbinding:
    case Intrinsic::spv_resource_handlefrombinding:
    case Intrinsic::spv_resource_handlefromimplicitbinding: {
      // (Roadmap F8a) A handle call whose own result has no uses at all is
      // not a resource access this target needs to normalize -- just dead
      // code -- the shape a SPIR-V `subpassInput` variable's `Dim::
      // SubpassData` handle is always left in once `feme::spirv::
      // SubpassLoadPattern` (SPIRVToLLVMPatterns.cpp) consumes its
      // `ImageRead` directly, without ever referencing the descriptor
      // handle the variable's own `handlefrombinding` still produces (a
      // subpass input is read from the currently-bound render-target
      // attachment, not that descriptor's image memory).
      bool AllAcceptedAsRootConstants =
          llvm::all_of(F.users(), [&](const User *U) {
            const auto *CI = dyn_cast<CallInst>(U);
            if (!CI)
              return false;
            return CI->use_empty() || AcceptedRootConstantHandles.contains(CI);
          });
      if (AllAcceptedAsRootConstants)
        continue;

      // A register-bound resource handle that survives to this point is
      // one `feme::cpu::BoundResourceNormalizationPass` could not (or does
      // not yet) normalize into a heap access -- an unbounded range, a
      // conflicting re-declaration of the same binding, an overflowing
      // combined heap prefix, an unsupported
      // resource kind, an implicit binding, or a SPIR-V binding (see
      // "Bound-resource normalization" in feme/docs/FeMeCPUDesign.md for
      // exactly which of these that pass accepts), and is not the one
      // register-bound constant buffer the root-constant block covers
      // either. The CPU target has no other way to address such a
      // resource: bindless (ResourceDescriptorHeap/SamplerDescriptorHeap)
      // access, a finite, unambiguous traditional binding, or that one
      // root-constant binding, are the only forms it accepts.
      //
      // Roadmap L64: the trailing hint matters. A resource lowering pass
      // that rejects one unsupported *use* leaves every handle in that
      // function unlowered, so the handle named here is whichever
      // declaration happens to come first in the module -- frequently an
      // innocent bystander rather than the resource whose use actually
      // failed. Reading this diagnostic literally cost several sessions,
      // which mis-filed an over-strict `Grad` derivative-width check
      // against arrayed image samples as an unrelated `spirv.VulkanBuffer`
      // uniform-block defect, purely because a scale/bias uniform block
      // was declared ahead of the image it accompanied.
      return createStringError(
          inconvertibleErrorCode(),
          "unsupported raised operation: '%s' is a register-bound resource "
          "handle the FeMe CPU target cannot normalize into a heap access "
          "or the root-constant block (an unbounded range, a conflicting "
          "re-declaration, an overflowing heap prefix, or an unsupported "
          "resource kind); express it as "
          "a finite, unambiguous traditional binding, bindless "
          "(ResourceDescriptorHeap/SamplerDescriptorHeap) access, or the "
          "one recognized root-constant binding. Note that an unsupported "
          "use of any *other* resource in the same function (for example an "
          "image sampled in a way this target cannot lower) also prevents "
          "every handle in that function from being normalized, so this "
          "handle may be an unrelated bystander rather than the real cause",
          F.getName().str().c_str());
    }
    default:
      break;
    }
  }
  return Error::success();
}

} // namespace feme::cpu
