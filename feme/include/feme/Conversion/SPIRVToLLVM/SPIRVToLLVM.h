//===- SPIRVToLLVM.h - spirv dialect -> llvm dialect (FeMe) -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares FeMe's `spirv` dialect -> `llvm` dialect conversion,
// which extends MLIR's stock `convert-spirv-to-llvm` with the pieces a
// module destined for LLVM's in-tree `SPIRV` backend needs and MLIR's
// conversion (written for the SPIR-V *runner*, which executes shaders on the
// host) has no reason to emit:
//
// - the target triple and data layout the SPIR-V module was compiled for,
//   as the `llvm.target_triple`/`llvm.data_layout` module attributes
//   `mlir::translateModuleToLLVMIR` forwards onto the `llvm::Module`, and
// - the `llvm.spv.*` target intrinsics that backend consumes, emitted as
//   `llvm.call_intrinsic` ops, for the SPIR-V constructs with no
//   target-independent LLVM equivalent at all (resource handles, image
//   accesses, builtin input variables).
//
// Naming those intrinsics is only meaningful once the module says which
// target it is for, which is why both live in one pass. See the "SPIR-V ->
// MLIR `llvm` dialect -> LLVM IR" section of feme/docs/Design.md.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_CONVERSION_SPIRVTOLLVM_SPIRVTOLLVM_H
#define FEME_CONVERSION_SPIRVTOLLVM_SPIRVTOLLVM_H

#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

#include <memory>
#include <string>

namespace llvm {
class Module;
} // namespace llvm

namespace mlir {
class LLVMTypeConverter;
class Operation;
class Pass;
class RewritePatternSet;
} // namespace mlir

namespace feme {
namespace spirv {

/// Returns the LLVM target triple describing the execution environment
/// \p Module is written for: a Vulkan shader triple naming the pipeline stage
/// of its entry points (e.g. "spirv-unknown-vulkan-compute"), or an OpenCL
/// flavored one ("spirv32"/"spirv64-unknown-unknown") for `Kernel` entry
/// points, whose bitness comes from the module's addressing model.
std::string getTargetTriple(mlir::spirv::ModuleOp Module);

/// What `llvm.spv.resource.handlefrombinding` needs to know about a SPIR-V
/// resource variable -- an image, a sampler, or a storage/uniform buffer
/// block -- recovered from its declaration before the conversion drops it.
struct ResourceInfo {
  uint32_t DescriptorSet;
  uint32_t Binding;
  /// Symbol of the string global naming the resource. LLVM's SPIRV backend
  /// reads its contents to name the `OpVariable` it emits, so the name has to
  /// exist in the module as real data rather than as an attribute.
  std::string NameSymbol;
  /// The number of descriptors this binding covers: 1 for an ordinary
  /// (non-arrayed) resource, or the declared length of an array-of-blocks
  /// binding (`T blocks[N]` in GLSL). An arrayed block's handle needs which
  /// descriptor to bind, only known at its own access chain's leading
  /// (array) index, so its `spirv.mlir.addressof` is erased rather than
  /// converted (see feme::spirv::populateSPIRVToLLVMTargetPatterns's
  /// `ArrayedBlockAccessChainPattern`).
  uint32_t Count = 1;
};

/// Resource variables, keyed by the symbol declaring them.
using ResourceInfoMap = llvm::StringMap<ResourceInfo>;

/// Recovers \p Module's resource variables, and materializes the name strings
/// their handles have to point at. Must run before the conversion, which
/// drops the declarations this reads.
ResourceInfoMap prepareResourceVariables(mlir::spirv::ModuleOp Module);

/// The address space (7 for `Input`, 8 for `Output`) a non-builtin stage-IO
/// variable's `llvm.mlir.addressof` converts to (see
/// populateSPIRVToLLVMTargetTypeConversions), keyed by the symbol declaring
/// it.
using StageIOInfoMap = llvm::StringMap<unsigned>;

/// Recovers the address space of every non-builtin `Input`/`Output`
/// variable \p Module declares. Must run before the conversion: unlike a
/// resource or builtin variable, a stage-IO variable's declaration survives
/// as a real `llvm.mlir.global`, but by the time its `spirv.mlir.addressof`
/// use is legalized, `spirv.GlobalVariable` siblings earlier in the same
/// block (this one included) may already have been converted, so looking
/// its storage class back up through the (by-then-replaced) declaration is
/// not reliable -- see StageIOAddressOfPattern.
StageIOInfoMap prepareStageIOVariables(mlir::spirv::ModuleOp Module);

/// The `VK_KHR_shader_float_controls` `RoundingModeRTZ` execution mode's
/// declared bit widths (16/32/64), keyed by the entry point (`spirv.func`
/// symbol) that declared it (roadmap F15a). Absent from the map entirely for
/// an entry point that never declared the mode. Recovered from
/// `spirv.ExecutionMode` before the conversion drops it (see
/// ConvertSPIRVToLLVMPass.cpp's collectEntryPoints), since by the time an
/// arithmetic FP op's own conversion pattern runs, the execution mode may
/// already have been legalized away. `DenormFlushToZero`'s own declared
/// widths (roadmap F15b) are recovered into a distinct map of this same
/// type, since an entry point may declare either, both (for the same or
/// different widths), or neither.
///
/// `VK_KHR_shader_float_controls2`'s own `FPRoundingMode`/`FPFastMathMode`
/// decorations (roadmap F15c) need no map of this kind at all: unlike this
/// whole-entry-point execution mode, they are per-instruction decorations
/// MLIR's deserializer already attaches directly to the individual
/// `spirv.FAdd`/etc. op they decorate, so the arithmetic op conversion
/// patterns below read them straight off \p Op, with no separate
/// before-conversion collection pass needed.
using FloatControlInfoMap = llvm::StringMap<llvm::SmallVector<unsigned, 3>>;

/// `VK_KHR_shader_float_controls2`'s own `FPFastMathDefault` execution mode
/// (roadmap F15d): unlike `FPRoundingMode`/`FPFastMathMode` above, this is a
/// whole-entry-point default -- one `spirv.ExecutionModeId` per
/// floating-point type the entry point declares a default `FPFastMathMode`
/// for -- rather than a per-instruction decoration, so it does need a
/// before-conversion collection pass the same way `RoundingModeRTZWidths`
/// does: an arithmetic op's own conversion pattern only ever sees the op
/// itself, not the module-level `spirv.ExecutionModeId` that names its
/// type's default. Keyed by entry point, then by that type's bit width
/// (16/32/64); absent from the inner map entirely for a width the entry
/// point declared no default for. An arithmetic op of that width carrying
/// its own `fp_fast_math_mode` decoration still takes priority over this
/// default for that one instruction, the same "decoration overrides
/// entry-point-wide default" precedence `RoundingModeRTZWidths`/
/// `FPRoundingMode` already established for `RoundingModeRTZ`.
using FastMathDefaultMap =
    llvm::StringMap<llvm::DenseMap<unsigned, mlir::spirv::FPFastMathMode>>;

/// Adds the type conversions the patterns below rely on to \p TypeConverter,
/// which must already have been populated with
/// `mlir::populateSPIRVToLLVMTypeConversion`: they take precedence over the
/// conversions registered before them.
void populateSPIRVToLLVMTargetTypeConversions(
    mlir::LLVMTypeConverter &TypeConverter);

/// Populates \p Patterns with FeMe's own `spirv` -> `llvm` dialect
/// conversion patterns: the ones MLIR has none for at all, plus the ones
/// where MLIR's conversion targets the SPIR-V runner rather than LLVM's
/// SPIRV backend. They are given a higher benefit than MLIR's, so they win
/// wherever both apply, and are meant to be used alongside (not instead of)
/// `mlir::populateSPIRVToLLVMConversionPatterns`.
/// \p Resources must have been collected by prepareResourceVariables, and
/// \p StageIOVariables by prepareStageIOVariables; both must outlive
/// \p Patterns. \p RoundingModeRTZWidths and \p DenormFlushToZeroWidths,
/// likewise outliving \p Patterns, are recovered by
/// ConvertSPIRVToLLVMPass.cpp's collectEntryPoints -- FeMe's own pass, not
/// this file, since they are read from `spirv.ExecutionMode`, an op outside
/// a `spirv.func` body these per-op conversion patterns otherwise never see.
/// \p FastMathDefaults, likewise recovered by collectEntryPoints (this time
/// from `spirv.ExecutionModeId`), is `FPFastMathDefault`'s own per-type
/// default (roadmap F15d).
void populateSPIRVToLLVMTargetPatterns(
    const mlir::LLVMTypeConverter &TypeConverter,
    mlir::RewritePatternSet &Patterns, const ResourceInfoMap &Resources,
    const StageIOInfoMap &StageIOVariables,
    const FloatControlInfoMap &RoundingModeRTZWidths,
    const FloatControlInfoMap &DenormFlushToZeroWidths,
    const FastMathDefaultMap &FastMathDefaults);

/// Creates the pass converting every `spirv.module` nested in a builtin
/// module into the `llvm` dialect, targeting LLVM's in-tree `SPIRV` backend.
/// This is a superset of MLIR's own `convert-spirv-to-llvm` -- see this
/// file's header comment for what it adds.
std::unique_ptr<mlir::Pass> createConvertSPIRVToLLVMPass();

/// The `feme-opt`/`--pass-pipeline` name of the pass above.
llvm::StringRef getConvertSPIRVToLLVMPassArgument();

/// The name of the `llvm.mlir.global` attribute the stage-IO global variable
/// pattern (see populateSPIRVToLLVMTargetPatterns) records a non-builtin
/// `Input`/`Output` variable's `Location`/`Component`/`Index`/interpolation/
/// per-primitive/per-patch decorations under: an `ArrayAttr` of `ArrayAttr`s,
/// each inner one an `(i32 decoration, i32 arg...)` tuple in the same shape
/// `!spirv.Decorations` metadata uses (see
/// `llvm/lib/Target/SPIRV/SPIRVUtils.cpp`'s `buildOpSpirvDecorations`).
/// `attachStageIODecorations` converts this attribute into that real LLVM
/// metadata once a genuine `llvm::Module` exists to attach it to.
llvm::StringRef getStageIODecorationsAttrName();

/// A non-builtin stage-IO global's decorations (see
/// getStageIODecorationsAttrName), keyed by the `llvm.mlir.global`'s symbol
/// name.
using StageIODecorationsMap = llvm::StringMap<mlir::ArrayAttr>;

/// Collects every `llvm.mlir.global` in \p Module (the `llvm` dialect module
/// `createConvertSPIRVToLLVMPass` produces) carrying a
/// getStageIODecorationsAttrName() attribute, keyed by symbol name. Must run
/// before `mlir::translateModuleToLLVMIR`, whose result
/// attachStageIODecorations re-attaches this information to: that translation
/// has no way to carry an attribute it does not understand from a dialect it
/// has no `LLVMTranslationDialectInterface` for.
StageIODecorationsMap collectStageIODecorations(mlir::Operation *Module);

/// Re-attaches \p Decorations (from collectStageIODecorations, run on the
/// `llvm` dialect module \p LLVMModule was translated from) as
/// `!spirv.Decorations` metadata on the matching `llvm::GlobalVariable`s in
/// \p LLVMModule, looked up by name. A no-op for any name \p LLVMModule does
/// not declare a global under (e.g. one dead-code-eliminated during
/// translation).
void attachStageIODecorations(const StageIODecorationsMap &Decorations,
                              llvm::Module &LLVMModule);

/// The name of the `llvm.mlir.global` attribute the stage-IO global variable
/// pattern (see populateSPIRVToLLVMTargetPatterns) records a builtin
/// interface block's (e.g. `gl_PerVertex`) own *per-member* decorations
/// under (roadmap H2c): an `ArrayAttr` of `(memberIndex, tuples...)`
/// entries, one per struct member carrying a recognized decoration, where
/// `tuples` is itself an `ArrayAttr` of `(i32 decoration, i32 arg...)`
/// tuples in the same shape getStageIODecorationsAttrName()'s whole-variable
/// attribute uses. Unlike that whole-variable attribute -- which
/// `attachStageIODecorations` turns into the real `!spirv.Decorations`
/// metadata LLVM's SPIRV backend understands -- this is a FeMe-internal
/// encoding with no backend equivalent (SPIR-V's own `OpMemberDecorate`
/// decorates a *type*, not a global variable, so there is nothing for the
/// backend to attach a per-member decoration to at the global-variable
/// granularity this metadata channel uses); it exists purely for
/// `feme::graphics::CanonicalizeStagePass` (roadmap H2d) to recover which
/// system value/`Location` each member of an interface block corresponds
/// to.
llvm::StringRef getStageIOMemberDecorationsAttrName();

/// A builtin interface block's per-member decorations (see
/// getStageIOMemberDecorationsAttrName), keyed by the `llvm.mlir.global`'s
/// symbol name.
using StageIOMemberDecorationsMap = llvm::StringMap<mlir::ArrayAttr>;

/// Collects every `llvm.mlir.global` in \p Module carrying a
/// getStageIOMemberDecorationsAttrName() attribute, keyed by symbol name.
/// Must run before `mlir::translateModuleToLLVMIR`, the same way
/// collectStageIODecorations does, and for the same reason.
StageIOMemberDecorationsMap
collectStageIOMemberDecorations(mlir::Operation *Module);

/// Re-attaches \p MemberDecorations (from collectStageIOMemberDecorations,
/// run on the `llvm` dialect module \p LLVMModule was translated from) as
/// `feme.spirv.MemberDecorations` metadata on the matching
/// `llvm::GlobalVariable`s in \p LLVMModule, looked up by name. A no-op for
/// any name \p LLVMModule does not declare a global under.
void attachStageIOMemberDecorations(
    const StageIOMemberDecorationsMap &MemberDecorations,
    llvm::Module &LLVMModule);

} // namespace spirv
} // namespace feme

#endif // FEME_CONVERSION_SPIRVTOLLVM_SPIRVTOLLVM_H
