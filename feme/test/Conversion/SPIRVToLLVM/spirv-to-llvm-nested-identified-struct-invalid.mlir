// RUN: feme-opt --feme-convert-spirv-to-llvm --verify-diagnostics --split-input-file %s

// Roadmap L5: a runtime array whose element is itself an *identified*
// (named) `spirv.struct` -- FeMe's own upstream HLSL resource
// representation's usual shape for `RWStructuredBuffer<T>`/
// `StructuredBuffer<T>` whenever `T` is a user-defined struct with an
// odd-width vector or non-natural-ABI member offset (which routes it past
// FeMe's own dedicated block-conversion pattern and into upstream MLIR's
// generic fallback) -- used to crash `mlir::VulkanLayoutUtils::decorateType`
// outright: `decorateType(spirv::StructType, ...)` deliberately returns
// `nullptr` for an identified struct nested inside another type (an
// identified struct is uniqued by name, so it cannot safely be given a
// second, possibly different layout), but every one of its own callers
// (`decorateType(ArrayType/RuntimeArrayType/VectorType/MatrixType, ...)`)
// fed that `nullptr` straight into `Type::get(...)` without checking,
// segfaulting instead of failing gracefully. Fixed in
// `mlir/lib/Dialect/SPIRV/Utils/LayoutUtils.cpp` by propagating the
// `nullptr` up instead. This is not yet a supported shape (see roadmap
// L13): the fix only turns the crash into the ordinary, diagnosable
// "failed to legalize" every other unsupported shape already produces.

spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @Out bind(0, 0) : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.S2, (!spirv.rtarray<!spirv.struct<S2, (vector<3xsi32> [0], si32 [12])>, stride=16> [0]), Block>, StorageBuffer>
  spirv.func @runtime_array_of_identified_struct(%idx : i32) -> si32 "None" {
    %addr = spirv.mlir.addressof @Out : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.S2, (!spirv.rtarray<!spirv.struct<S2, (vector<3xsi32> [0], si32 [12])>, stride=16> [0]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : si32
    %c1 = spirv.Constant 1 : si32
    // expected-error@+1 {{failed to legalize operation 'spirv.AccessChain' that was explicitly marked illegal}}
    %ac = spirv.AccessChain %addr[%c0, %idx, %c1] : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.S2, (!spirv.rtarray<!spirv.struct<S2, (vector<3xsi32> [0], si32 [12])>, stride=16> [0]), Block>, StorageBuffer>, si32, i32, si32 -> !spirv.ptr<si32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : si32
    spirv.ReturnValue %v : si32
  }
}

