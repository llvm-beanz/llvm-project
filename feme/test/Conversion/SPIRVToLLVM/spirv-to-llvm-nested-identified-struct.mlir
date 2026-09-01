// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L5: a runtime array whose element is itself an *identified*
// (named) `spirv.struct` -- FeMe's own upstream HLSL resource
// representation's usual shape for `RWStructuredBuffer<T>`/
// `StructuredBuffer<T>` whenever `T` is a user-defined struct with an
// odd-width vector or non-natural-ABI member offset (which routes it past
// FeMe's own dedicated block-conversion pattern and into upstream MLIR's
// generic fallback) -- used to crash `mlir::VulkanLayoutUtils::decorateType`
// outright. Fixed in `mlir/lib/Dialect/SPIRV/Utils/LayoutUtils.cpp` by
// propagating a `nullptr` member layout up instead of dereferencing it.
//
// Roadmap L13: at that point this exact shape (`S2 { int3 Legs; int
// TailState; }`, whose declared offset for `TailState` -- 12 -- assumes
// `Legs`'s own `vector<3xsi32>` is tightly packed, 12 bytes, not LLVM's
// rounded-to-vec4 16 bytes) still failed to convert at all (gracefully,
// via "failed to legalize operation 'spirv.AccessChain'"), since
// `convertOffsetStructTypeIgnoringDecorations`'s natural-ABI layout check
// (`SPIRVToLLVMPatterns.cpp`) couldn't reproduce that offset with a real
// `vector<3xsi32>` member. Fixed by that function's own "tight-vector
// retry": substituting a same-bit-width `!llvm.array<3 x i32>` for the
// `Legs` member instead, whose own natural ABI alignment/size (4-byte
// aligned, 12 bytes) matches the declared offset exactly. This is the
// same struct shape (nested one level inside a runtime array) as roadmap
// L5's own regression above; now confirmed to fully convert successfully.

// CHECK-LABEL: llvm.func @runtime_array_of_identified_struct
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: !llvm.target<"spirv.VulkanBuffer", !llvm.array<0 x struct<(array<3 x i32>, i32)>>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]]
// CHECK: %[[GEP:.*]] = llvm.getelementptr inbounds %[[PTR]][0, 1]
// CHECK: %[[VAL:.*]] = llvm.load %[[GEP]]
// CHECK: llvm.return %[[VAL]]

spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @Out bind(0, 0) : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.S2, (!spirv.rtarray<!spirv.struct<S2, (vector<3xsi32> [0], si32 [12])>, stride=16> [0]), Block>, StorageBuffer>
  spirv.func @runtime_array_of_identified_struct(%idx : i32) -> si32 "None" {
    %addr = spirv.mlir.addressof @Out : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.S2, (!spirv.rtarray<!spirv.struct<S2, (vector<3xsi32> [0], si32 [12])>, stride=16> [0]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : si32
    %c1 = spirv.Constant 1 : si32
    %ac = spirv.AccessChain %addr[%c0, %idx, %c1] : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.S2, (!spirv.rtarray<!spirv.struct<S2, (vector<3xsi32> [0], si32 [12])>, stride=16> [0]), Block>, StorageBuffer>, si32, i32, si32 -> !spirv.ptr<si32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : si32
    spirv.ReturnValue %v : si32
  }
}
