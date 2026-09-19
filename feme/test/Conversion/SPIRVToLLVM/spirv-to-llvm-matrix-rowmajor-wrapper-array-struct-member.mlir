// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L124(r): a `RowMajor`+non-natural-`MatrixStride` matrix reached
// through a `StructuredBuffer<S>`/`RWStructuredBuffer<S>`-style wrapper
// (a storage buffer block whose sole member is a dynamically-indexed
// array), where the array's own *element* is itself a struct `S`
// containing one or more matrix members -- e.g. `struct S { mat2x2 a;
// float4 other; mat2x2 b; };` -- rather than the array wrapping the
// matrix directly (the shape `getMatrixWholeAccess`'s `HasWrapper`
// branch already handled, roadmap L83/L124(g)/(i)). Found via
// `dEQP-VK.ssbo.layout.random.nested_structs.12`, whose own
// decompiled SPIR-V (`--deqp-log-decompiled-spirv=enable`) showed
// exactly this shape.
//
// `getMatrixWholeAccess`'s `HasWrapper` branch peeled through however
// many array levels wrap the content, then required what remained to be
// directly a `MatrixType` -- it had no case for landing on a `StructType`
// instead, so this shape was rejected outright, falling back to the
// generic conversion. The matrix's own *type* was already correctly
// widened by the pre-existing struct-member conversion logic, but the
// whole-matrix `spirv.Store`/`spirv.Load` reinterpretation (transpose +
// per-row padding) never fired, silently storing/loading the plain
// logical (unpadded, column-major) value straight into the wider
// physical layout -- a memory-size mismatch, not merely a wrong value.
//
// Fixed by extracting the non-wrapper branch's own struct-member walk
// (added for L124(o)/L124(q)) into a shared `walkStructMembersToMatrix`
// helper, and using it from the `HasWrapper` branch too whenever the
// array's own peeled element type is a struct rather than a matrix,
// continuing the walk from there with whatever indices remain past the
// wrapper's own dummy index and array levels.

// CHECK-LABEL: llvm.func @store_load
// CHECK-SAME: (%[[M:.*]]: !llvm.array<2 x vector<2xf32>>, %[[IDX:.*]]: i32) -> !llvm.array<2 x vector<2xf32>>
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.array<0 x struct<packed (array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>, vector<4xf32>, array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>, array<16 x i8>)>>, 12, 1, 96>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %[[IDX]])
// CHECK: %[[ELEM:.*]] = llvm.getelementptr inbounds %[[PTR]][0, 2]
// CHECK-SAME: -> !llvm.ptr<12>, !llvm.struct<packed (array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>, vector<4xf32>, array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>)>
//
// The value stored is transposed (RowMajor) and padded (each row's own
// natural 8-byte size padded up to the declared 16-byte MatrixStride)
// into the physical shape before the real `llvm.store`, and the inverse
// happens after the real `llvm.load` -- both crossing exactly the same
// flat, packed, per-row-padded type reached by `%[[ELEM]]`.
// CHECK: llvm.store %{{.*}}, %[[ELEM]] : !llvm.array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>, !llvm.ptr<12>
// CHECK: %[[LOADED:.*]] = llvm.load %[[ELEM]] : !llvm.ptr<12> -> !llvm.array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>
// CHECK: llvm.return %{{.*}} : !llvm.array<2 x vector<2xf32>>
!S = !spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, RowMajor, MatrixStride=16], vector<4xf32> [32], !spirv.matrix<2 x vector<2xf32>> [48, RowMajor, MatrixStride=16])>
!Wrap = !spirv.struct<(!spirv.rtarray<!S, stride=96>), BufferBlock>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ssbo bind(0, 0) : !spirv.ptr<!Wrap, Uniform>
  spirv.func @store_load(%m : !spirv.matrix<2 x vector<2xf32>>, %idx : si32) -> !spirv.matrix<2 x vector<2xf32>> "None" {
    %addr = spirv.mlir.addressof @ssbo : !spirv.ptr<!Wrap, Uniform>
    %c0 = spirv.Constant 0 : si32
    %c2 = spirv.Constant 2 : si32
    %elem = spirv.AccessChain %addr[%c0, %idx, %c2] : !spirv.ptr<!Wrap, Uniform>, si32, si32, si32 -> !spirv.ptr<!spirv.matrix<2 x vector<2xf32>>, Uniform>
    spirv.Store "Uniform" %elem, %m : !spirv.matrix<2 x vector<2xf32>>
    %v = spirv.Load "Uniform" %elem : !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %v : !spirv.matrix<2 x vector<2xf32>>
  }
  spirv.EntryPoint "GLCompute" @store_load
  spirv.ExecutionMode @store_load "LocalSize", 1, 1, 1
}
