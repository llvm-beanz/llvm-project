//===- SPIRVResourceLowering.h - SPIR-V bound resource emulation -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::SPIRVResourceLoweringPass, the SPIR-V
// counterpart to `feme::cpu::BoundResourceNormalizationPass` +
// `feme::cpu::ResourceLoweringPass`: it rewrites a SPIR-V-sourced module's
// `spirv.VulkanBuffer` resource access directly into the canonical,
// type-mangled `feme.cpu.resource.*` calls those two DXIL-facing passes
// jointly produce (see `feme::cpu::ResourceCalls`), and attaches the same
// `feme.cpu.resources`/`feme.cpu.bound_resources` metadata they do -- so
// every later stage of the CPU pipeline, and the host-facing
// `feme::cpu::ResourceInfo`/`feme::cpu::ResourceHeap` machinery
// `feme::cpu::JITEngine`/`feme-run` use to supply a dispatch's bound
// resources, need no SPIR-V-specific case of their own at all.
//
// SPIR-V has no bindless descriptor-heap counterpart to DXIL's
// `ResourceDescriptorHeap` (see "Resource Model" in
// feme/docs/FeMeCPUDesign.md's SPIR-V bullet), so every SPIR-V resource is a
// traditional, register-bound one -- there is no dynamic-heap case to
// distinguish, unlike the DXIL side's two-pass split around
// `feme::cpu::checkSupportedRaisedOps`. This pass therefore normalizes and
// lowers a bound handle in one step rather than two: a (descriptor set,
// binding) identity plays the same role DXIL's (register space, register)
// does (see `feme::spirv::RaisedLoweringPass`'s header comment for the
// SPIR-V -> raised direction's own use of that same correspondence).
//
// Roadmap step R26 generalized this from an implicit range size of 1 to a
// real arrayed binding: `llvm.spv.resource.handlefrombinding`'s own range
// size and index operands (SPIR-V's descriptor-array count and array index,
// the same role DXIL's `register(t0, space0, numDescriptors=N)` range and
// `handlefrombinding`'s own dynamic index operand play) are read and
// normalized exactly like `feme::cpu::BoundResourceNormalizationPass` does
// for DXIL: each (set, binding) identity's declared array is assigned a
// contiguous run of heap slots, and an access through it is rewritten into
// `HeapBase + clamp(Index, RangeSize)`, with an out-of-range index or an
// overflow while forming the physical heap index selecting the same
// `UINT32_MAX` out-of-heap sentinel the DXIL side uses (see that pass's
// header comment and "Bound-resource normalization" in
// feme/docs/FeMeCPUDesign.md). A Vulkan *dynamic* storage/uniform buffer
// offset (`VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC`/
// `..._UNIFORM_BUFFER_DYNAMIC`) needs no shader-side support at all: per
// "Memory and Buffers" in feme/docs/FeMeVulkanDesign.md, a dynamic offset is
// folded into the `FemeDescriptor::Data` pointer the host materializes for
// a dispatch, exactly like every other buffer's binding offset -- this pass
// (and the `FemeDescriptor` it lowers accesses to reference) look identical
// whether or not the descriptor behind a given heap slot happens to be a
// dynamic one.
//
// Scope (roadmap steps R10, R26, V3, F12a; see feme/docs/Roadmap.md's
// §1.2/§1.9):
//
//  - A `StorageBuffer`-derived `spirv.VulkanBuffer` handle (an
//    `RWStructuredBuffer<T>`/`StructuredBuffer<T>` in HLSL, see
//    `feme::spirv::convertBufferBlockType` in SPIRVToLLVMPatterns.cpp) and a
//    `Uniform`-derived one (a `cbuffer`/`ConstantBuffer<T>`, see
//    `feme::spirv::convertUniformBlockType`) are both normalized. So is a
//    uniform buffer whose sole field is itself a fixed-size array (roadmap
//    F12a: `layout(std140) uniform Input { uint data[16]; }`), normalized
//    the same way a storage buffer's own runtime array is -- see
//    `feme::spirv::getUniformBlockElement`'s comment for why this shape
//    needs its own handling, since a std140 array's declared `ArrayStride`
//    need not equal its element's own natural size the way a std430
//    storage buffer array's always does. (V4) A `Dim::Buffer` image handle
//    (`Buffer<T>`/`RWBuffer<T>` in HLSL -- Vulkan's uniform/storage texel
//    buffer) *is* normalized, but only over a `<4 x float>` or `<4 x i32>`
//    shader-side element -- see `isSupportedTexelElementType`'s comment for
//    why this milestone's CPU runtime only supports those two shapes (every
//    real per-format texel fetch/write is still a full four-component
//    vector, per SPIR-V's own `OpImageRead`/`OpImageFetch`/`OpImageWrite`
//    semantics; a physically narrower channel count needs per-format
//    padding this milestone does not add).
//  - For a storage buffer, or a uniform buffer array, only the access shape
//    `feme::spirv::BlockAccessChainPattern` itself produces for a flat
//    (non-aggregate) buffer element -- a direct
//    `llvm.spv.resource.getpointer` followed immediately by an ordinary
//    `load`/`store` (a uniform buffer array's own `store` is never actually
//    produced by a legal shader, but is rejected the same way a plain
//    uniform buffer's is below regardless), with no intervening
//    `getelementptr` into the element's own fields -- is rewritten. A
//    structured-buffer element with fields accessed individually is left
//    untouched, exactly as `feme::cpu::ResourceLoweringPass` leaves any
//    access shape it does not itself model.
//  - For a (non-array) uniform buffer, only the analogous shape
//    `feme::spirv::BlockAccessChainPattern` produces -- a direct
//    `llvm.spv.resource.getpointer` selecting one field of the block's own
//    struct (its index always a compile-time constant, unlike a storage
//    buffer's, or a uniform buffer array's, array index) followed
//    immediately by an ordinary `load` -- is rewritten; the field index
//    resolves directly to that field's compile-time struct-layout byte
//    offset, with no runtime multiplication needed. There is no `store`
//    case at all: Vulkan disallows writing `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`.
//    A nested field within one of the block's own struct- or array-typed
//    fields is left untouched, the same "flat access only" narrowing the
//    storage-buffer case above uses.
//  - A binding's range size must be a compile-time constant (matching the
//    set/binding identity itself); its array index need not be -- a
//    dynamic index is accepted and clamped at run time, exactly as
//    `feme::cpu::BoundResourceNormalizationPass` accepts a dynamic
//    `handlefrombinding` index.
//  - An unbounded range (range size 0, SPIR-V's own spelling of an
//    unbounded descriptor array) is left un-normalized, matching the DXIL
//    side's own rejection of an unbounded `handlefrombinding` range. (roadmap
//    L12c) This pass itself never learns to resolve one: `feme::vulkan`'s
//    compute/graphics pipeline compilation instead rewrites an unbounded
//    range's operand to the matching `VkPipelineLayout` binding's own
//    declared count *before* this pass ever runs (see
//    `patchUnboundedResourceRanges` in feme/lib/Vulkan/Pipeline.h/.cpp) --
//    from this pass's own perspective, a shader using an unbounded array is
//    indistinguishable from one declaring an ordinary bounded array whose
//    size happens to come from the pipeline layout instead of the shader's
//    own SPIR-V type.
//  - (Roadmap R30) A bound 2D *sampled image* handle and a `spirv.Sampler`
//    handle are normalized too, into the *image* and *sampler* heaps rather
//    than the buffer-oriented resource heap -- the three are separate
//    arrays with independently numbered slots, so each range records the
//    `feme::cpu::BoundResourceClass` its own heap base indexes. Their
//    accesses lower to the canonical `feme.cpu.image.*` calls (ImageCalls.h)
//    the DXIL bindless path already produces, not to
//    `feme.cpu.resource.*`: `llvm::spv::resource::sample`/`samplelevel`
//    become `feme.cpu.image.sample.2d.v4f32` (implicit LOD asking for level
//    0, explicit LOD threading its own operand through), and an
//    `OpImageFetch`'s `getpointer`+`load` pair becomes
//    `feme.cpu.image.load.2d.v4f32`. The `{image, sampler}` struct
//    `feme::spirv::SampledImagePattern` builds for `OpSampledImage` is
//    folded away first (`foldSampledImageStructs`), since FeMe's image ABI
//    keeps the two descriptors separate throughout.
//
//    Scoped out for the same reasons the DXIL side scopes them out (see
//    "Canonical image operations" in feme/docs/FeMeGraphicsDesign.md): any
//    dimension other than 2D, an arrayed or multisampled image, a storage
//    image (`Sampled == 2`, which would need a `feme.cpu.image.store.*`
//    helper `runtime/CPU` does not implement), a non-`f32` channel type,
//    and a nonzero texel offset. Each is left un-normalized rather than
//    approximated, so `feme::cpu::checkSupportedRaisedOps` still rejects it.
//  - As with the DXIL passes this mirrors, an unsupported access shape or a
//    conflicting re-declaration of the same (descriptor set, binding)
//    identity (two handles disagreeing about the buffer's kind, element
//    stride/struct layout, or the array's range size) leaves every handle
//    at that identity un-normalized, so `feme::cpu::checkSupportedRaisedOps`
//    still rejects it.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_SPIRVRESOURCELOWERING_H
#define FEME_TRANSFORMS_CPU_SPIRVRESOURCELOWERING_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Normalizes and lowers SPIR-V bound resource access -- storage, uniform
/// and texel buffers, plus 2D sampled images and samplers -- directly into
/// the same canonical `feme.cpu.resource.*`/`feme.cpu.image.*` calls the
/// DXIL `BoundResourceNormalizationPass` + `ResourceLoweringPass` pair
/// produces. See the file comment above for current scope.
class SPIRVResourceLoweringPass
    : public llvm::PassInfoMixin<SPIRVResourceLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-lower-spirv-resources"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_SPIRVRESOURCELOWERING_H
