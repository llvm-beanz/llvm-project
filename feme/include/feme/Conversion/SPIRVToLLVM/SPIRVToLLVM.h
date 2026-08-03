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
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

#include <memory>
#include <string>

namespace mlir {
class LLVMTypeConverter;
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
/// resource variable, recovered from its declaration before the conversion
/// drops it.
struct ResourceInfo {
  uint32_t DescriptorSet;
  uint32_t Binding;
  /// Symbol of the string global naming the resource. LLVM's SPIRV backend
  /// reads its contents to name the `OpVariable` it emits, so the name has to
  /// exist in the module as real data rather than as an attribute.
  std::string NameSymbol;
};

/// Resource variables, keyed by the symbol declaring them.
using ResourceInfoMap = llvm::StringMap<ResourceInfo>;

/// Recovers \p Module's resource variables, and materializes the name strings
/// their handles have to point at. Must run before the conversion, which
/// drops the declarations this reads.
ResourceInfoMap prepareResourceVariables(mlir::spirv::ModuleOp Module);

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
/// must outlive \p Patterns.
void populateSPIRVToLLVMTargetPatterns(
    const mlir::LLVMTypeConverter &TypeConverter,
    mlir::RewritePatternSet &Patterns, const ResourceInfoMap &Resources);

/// Creates the pass converting every `spirv.module` nested in a builtin
/// module into the `llvm` dialect, targeting LLVM's in-tree `SPIRV` backend.
/// This is a superset of MLIR's own `convert-spirv-to-llvm` -- see this
/// file's header comment for what it adds.
std::unique_ptr<mlir::Pass> createConvertSPIRVToLLVMPass();

/// The `feme-opt`/`--pass-pipeline` name of the pass above.
llvm::StringRef getConvertSPIRVToLLVMPassArgument();

} // namespace spirv
} // namespace feme

#endif // FEME_CONVERSION_SPIRVTOLLVM_SPIRVTOLLVM_H
