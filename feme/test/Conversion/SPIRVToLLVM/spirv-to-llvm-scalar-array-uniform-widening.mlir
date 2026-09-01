// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L17: the general (dynamic-index-capable) half of the fix --
// a fixed-size array of a *scalar* type whose declared `ArrayStride` (16)
// is wider than its own natural element size (4 bytes, `si32`), but that
// is *not* immediately followed by a sibling member needing its own last
// element's trailing padding reclaimed (here it is `CB`'s own only, and
// so final, member) -- a real `-fvk-use-dx-layout` shape
// (`Feature/CBuffer/array-dynamic-index.test`'s own `uint x[4]`).
//
// `convertArrayTypeIgnoringDecorations`'s own generalized scalar-array
// case (`SPIRVToLLVMPatterns.cpp`) substitutes a `Stride`-sized opaque
// byte-array stand-in for every element uniformly (mirroring
// `getTightVectorArrayType`'s identical "safe for pointer-based access"
// reasoning): this keeps the result a single, ordinary
// `LLVM::LLVMArrayType`, so unlike the trailing-sibling case (see
// spirv-to-llvm-scalar-array-with-trailing-sibling.mlir,
// convertUndersizedScalarArrayMemberIgnoringDecorations), a genuinely
// *dynamic* SPIR-V index into it converts just as well as a constant one.

// CHECK-LABEL: llvm.func @scalar_array_with_dynamic_index
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: !llvm.target<"spirv.VulkanBuffer", !llvm.array<0 x i32>, 2, 0, 16>

// `CB`'s own single member (`x`) unwraps directly to the resource's own
// type here, so `getpointer` itself takes the dynamic SPIR-V index
// directly (its own declared `16`-byte stride, from the target type
// above, supplies the per-element addressing math) -- no residual GEP is
// needed at all, and (unlike the trailing-sibling case) nothing here
// requires the index to be a compile-time constant:
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %arg0
// CHECK: %[[V:.*]] = llvm.load %[[PTR]]
// CHECK: llvm.return %[[V]]

spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @CB bind(0, 0) : !spirv.ptr<!spirv.struct<type.CB, (!spirv.array<4 x si32, stride=16> [0]), Block>, Uniform>
  spirv.func @scalar_array_with_dynamic_index(%idx : si32) -> si32 "None" {
    %addr = spirv.mlir.addressof @CB : !spirv.ptr<!spirv.struct<type.CB, (!spirv.array<4 x si32, stride=16> [0]), Block>, Uniform>
    %c0 = spirv.Constant 0 : si32
    %ac = spirv.AccessChain %addr[%c0, %idx] : !spirv.ptr<!spirv.struct<type.CB, (!spirv.array<4 x si32, stride=16> [0]), Block>, Uniform>, si32, si32 -> !spirv.ptr<si32, Uniform>
    %v = spirv.Load "Uniform" %ac : si32
    spirv.ReturnValue %v : si32
  }
}
