// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L13a: a fixed-size array whose element is itself an *identified*
// `spirv.struct` (`X`), with a declared `ArrayStride` (16) wider than `X`'s
// own natural size (4 bytes, a single `si32` member) -- a real
// `-fvk-use-dx-layout` shape (`X xs[2];` inside a `cbuffer`, each element
// starting its own fresh 16-byte HLSL register) -- used to fail to convert
// at all: MLIR's own `convertArrayType` (`SPIRVToLLVM.cpp`) validates a
// declared stride against `VulkanLayoutUtils::getNaturalArrayStride`, which
// unconditionally fails for *any* identified-struct element (see
// `VulkanLayoutUtils::decorateType`'s own "Identified structs are uniqued by
// identifier" comment, `LayoutUtils.cpp`), regardless of whether the stride
// is actually reproducible. Fixed by `convertArrayTypeIgnoringDecorations`
// (`SPIRVToLLVMPatterns.cpp`), which instead pads the element's own
// converted body with a trailing byte array up to the declared stride
// whenever it is itself an LLVM struct undershooting that stride -- this
// appends a new, otherwise-unreferenced member rather than wrapping the
// element in a new outer struct, so `X`'s own single real field stays at
// member index 0, and no `spirv.AccessChain` index sequence reaching into it
// needs to change to accommodate the padding.
//
// The containing struct (`CB`) also has two *non-array* members of the same
// identified struct type (`x1`, `x2`), each likewise needing to start its
// own fresh 16-byte register -- the same gap, but for an ordinary struct
// member rather than an array element (`xs`, `CB`'s own final member,
// needs no such padding of its own, since nothing declared after it needs
// to reclaim its own trailing space) -- fixed by
// `convertOffsetStructTypeIgnoringDecorations`'s own new growth retry
// (padUndersizedMembersIfNeeded), which pads `x1`'s and `x2`'s own
// converted types the same way, up to the declared gap to each one's own
// next sibling.

// CHECK-LABEL: llvm.func @array_and_member_of_identified_struct
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(struct<(i32, array<12 x i8>)>, struct<(i32, array<12 x i8>)>, array<2 x struct<(i32, array<12 x i8>)>>)>

// The array-element access (`xs[idx].a1`) needs no extra "unwrap" index:
// CHECK: %[[PTR0:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]]
// CHECK: %[[GEP0:.*]] = llvm.getelementptr inbounds %[[PTR0]][0, %{{.*}}, 0]
// CHECK: %[[V0:.*]] = llvm.load %[[GEP0]]

// The standalone-member access (`x2.a1`) needs no extra "unwrap" index either:
// CHECK: %[[PTR1:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]]
// CHECK: %[[GEP1:.*]] = llvm.getelementptr inbounds %[[PTR1]][0, 0]
// CHECK: %[[V1:.*]] = llvm.load %[[GEP1]]

// CHECK: %[[SUM:.*]] = llvm.add %[[V0]], %[[V1]]
// CHECK: llvm.return %[[SUM]]

spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @CB bind(0, 0) : !spirv.ptr<!spirv.struct<type.CB, (!spirv.struct<X, (si32 [0])> [0], !spirv.struct<X, (si32 [0])> [16], !spirv.array<2 x !spirv.struct<X, (si32 [0])>, stride=16> [32]), Block>, Uniform>
  spirv.func @array_and_member_of_identified_struct(%idx : si32) -> si32 "None" {
    %addr = spirv.mlir.addressof @CB : !spirv.ptr<!spirv.struct<type.CB, (!spirv.struct<X, (si32 [0])> [0], !spirv.struct<X, (si32 [0])> [16], !spirv.array<2 x !spirv.struct<X, (si32 [0])>, stride=16> [32]), Block>, Uniform>
    %c0 = spirv.Constant 0 : si32
    %c1 = spirv.Constant 1 : si32
    %c2 = spirv.Constant 2 : si32
    %ac0 = spirv.AccessChain %addr[%c2, %idx, %c0] : !spirv.ptr<!spirv.struct<type.CB, (!spirv.struct<X, (si32 [0])> [0], !spirv.struct<X, (si32 [0])> [16], !spirv.array<2 x !spirv.struct<X, (si32 [0])>, stride=16> [32]), Block>, Uniform>, si32, si32, si32 -> !spirv.ptr<si32, Uniform>
    %v0 = spirv.Load "Uniform" %ac0 : si32
    %ac1 = spirv.AccessChain %addr[%c1, %c0] : !spirv.ptr<!spirv.struct<type.CB, (!spirv.struct<X, (si32 [0])> [0], !spirv.struct<X, (si32 [0])> [16], !spirv.array<2 x !spirv.struct<X, (si32 [0])>, stride=16> [32]), Block>, Uniform>, si32, si32 -> !spirv.ptr<si32, Uniform>
    %v1 = spirv.Load "Uniform" %ac1 : si32
    %sum = spirv.IAdd %v0, %v1 : si32
    spirv.ReturnValue %sum : si32
  }
}
