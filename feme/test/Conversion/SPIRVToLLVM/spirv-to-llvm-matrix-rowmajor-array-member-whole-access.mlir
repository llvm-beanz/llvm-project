// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L124(q): a `RowMajor`+non-natural-`MatrixStride` matrix reached
// through a direct (non-wrapper) *array* member of a storage buffer block
// -- `struct { ...; matCxR m[N]; }`'s own `m[i]`, unlike the dxc/glslang
// dynamically-indexed-array wrapper shape (`HasWrapper=true`, handled by
// `getMatrixWholeAccess`'s own separate branch above this one) -- found
// via `dEQP-VK.ssbo.layout.random.basic_types.18`, whose own SSBO struct
// has an ordinary scalar/vector member *and* a `RowMajor`+`MatrixStride`
// `mat2x3` member wrapped in a one-element array.
//
// `getMatrixWholeAccess`'s non-wrapper branch (added for roadmap L124(o))
// only ever walked through nested *struct*-member selects before the
// final member select landing directly on a bare matrix -- it never
// expected that final member itself to be an *array* of matrices, so an
// AccessChain with one extra (array-index) selector past the member
// select was rejected outright, falling back to the generic,
// `MatrixStride`-unaware conversion. The matrix's own *type* was already
// widened correctly (`convertOffsetStructTypeIgnoringDecorations`'s
// array-of-matrix retry tier, roadmap H134, already handles this shape),
// but the whole-matrix `spirv.Store`/`spirv.Load` reinterpretation
// (transpose + per-row padding) that requires never fired, silently
// storing/loading the plain logical (column-major, unpadded) matrix value
// straight into the physical (row-major, padded) memory layout --
// corrupting every element past the first.
//
// Fixed by generalizing the non-wrapper branch's terminal case: once a
// struct-member select lands on a member that is directly an array
// (of however many levels) of matrices, rather than a bare matrix,
// require exactly that many further array-index selectors (mirroring
// the `HasWrapper` branch's own pre-existing array-nesting peel just
// above it) before treating the access as reaching the whole matrix,
// with the physical-layout decorations still read from the owning
// struct's own member (the array itself), not from inside it.

// CHECK-LABEL: llvm.func @store_load
// CHECK-SAME: (%[[M:.*]]: !llvm.array<2 x vector<3xf32>>) -> !llvm.array<2 x vector<3xf32>>
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (struct<"feme.tight_vector", (array<3 x i32>)>, i32, struct<"feme.tight_vector.1", (array<4 x f32>)>, array<1 x array<16 x i8>>, array<1 x array<3 x struct<packed (array<2 x f32>, array<8 x i8>)>>>)>, 12, 1>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[ELEM:.*]] = llvm.getelementptr inbounds %[[PTR]][0, %{{.*}}]
// CHECK-SAME: -> !llvm.ptr<12>, !llvm.array<1 x array<3 x struct<packed (array<2 x f32>, array<8 x i8>)>>>
//
// The value stored is transposed (RowMajor) and padded (each row's own
// natural 8-byte size padded up to the declared 16-byte MatrixStride)
// into the physical shape before the real `llvm.store`, and the inverse
// happens after the real `llvm.load` -- both crossing exactly the same
// flat, packed, per-row-padded type reached by `%[[ELEM]]`.
// CHECK: llvm.store %{{.*}}, %[[ELEM]] : !llvm.array<3 x struct<packed (array<2 x f32>, array<8 x i8>)>>, !llvm.ptr<12>
// CHECK: %[[LOADED:.*]] = llvm.load %[[ELEM]] : !llvm.ptr<12> -> !llvm.array<3 x struct<packed (array<2 x f32>, array<8 x i8>)>>
// CHECK: llvm.return %{{.*}} : !llvm.array<2 x vector<3xf32>>
!S = !spirv.struct<(vector<3xsi32> [0], si32 [12], vector<4xf32> [16], !spirv.array<1 x vector<2xsi32>, stride=16> [32], !spirv.array<1 x !spirv.matrix<2 x vector<3xf32>>, stride=48> [48, RowMajor, MatrixStride=16]), BufferBlock>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ssbo bind(0, 0) : !spirv.ptr<!S, Uniform>
  spirv.func @store_load(%m : !spirv.matrix<2 x vector<3xf32>>) -> !spirv.matrix<2 x vector<3xf32>> "None" {
    %addr = spirv.mlir.addressof @ssbo : !spirv.ptr<!S, Uniform>
    %c4 = spirv.Constant 4 : si32
    %c0 = spirv.Constant 0 : si32
    %elem = spirv.AccessChain %addr[%c4, %c0] : !spirv.ptr<!S, Uniform>, si32, si32 -> !spirv.ptr<!spirv.matrix<2 x vector<3xf32>>, Uniform>
    spirv.Store "Uniform" %elem, %m : !spirv.matrix<2 x vector<3xf32>>
    %v = spirv.Load "Uniform" %elem : !spirv.matrix<2 x vector<3xf32>>
    spirv.ReturnValue %v : !spirv.matrix<2 x vector<3xf32>>
  }
  spirv.EntryPoint "GLCompute" @store_load
  spirv.ExecutionMode @store_load "LocalSize", 1, 1, 1
}
