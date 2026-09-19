// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L124(t): a `RowMajor`+non-natural-`MatrixStride` matrix reached
// through a *trailing runtime-sized array* member of a plain (non-wrapper,
// multi-member) storage buffer block -- e.g.
// `buffer Block { vec3 c; int d; matCxR g[]; };`'s own `g[i]` -- was never
// physically widened at the type-conversion level. Found via
// `dEQP-VK.ssbo.layout.random.all_shared_buffer.41`, whose own decompiled
// SPIR-V (`--deqp-log-decompiled-spirv=enable`) showed exactly this shape:
// `OpTypeRuntimeArray` directly wrapping an `OpTypeMatrix`, decorated
// `RowMajor`/`MatrixStride` on the owning struct's own member.
//
// `peelArraysToMatrixType` (used by
// `convertOffsetStructTypeIgnoringDecorations`'s own per-member
// representability check, roadmap L124(g)) deliberately declined to peel
// through a `spirv::RuntimeArrayType` at all, reasoning that it may only
// ever be a struct's own last member so a fixed-size-array peel is
// sufficient -- but that reasoning missed that the runtime array *itself*
// can directly wrap a matrix (not merely nest inside one), which is
// exactly this shape. Because of this, `isMatrixMemberLayoutRepresentable`
// (the fallback used whenever `peelArraysToMatrixType` returns null) never
// saw the real inner matrix type at all, so this member was always
// silently treated as "naturally representable" regardless of its real
// `RowMajor`/`MatrixStride` decorations -- the runtime array's own type
// conversion (`RuntimeArrayType`'s own `TypeConverter.addConversion`
// lambda) then emitted the plain, natural (unwidened, untransposed,
// column-major) matrix shape, whose natural per-element size differs from
// the declared `ArrayStride` for a non-square matrix like `mat4x3`
// (RowMajor: 3 rows of 4 floats = 16 bytes/row, 48 bytes total; natural
// column-major: 4 columns of `vector<3xf32>`, each padded by LLVM's data
// layout to 16 bytes = 64 bytes total) -- silently corrupting every
// element's own addressing.
//
// Fixed by extending both `peelArraysToMatrixType` and its own inverse,
// `wrapPhysicalMatrixInArrays`, to also peel/rewrap a single trailing
// `spirv::RuntimeArrayType` (as an unsized `!llvm.array<0 x T>`, mirroring
// `RuntimeArrayType`'s own conversion), so this member gets the same
// physical (RowMajor/MatrixStride-substituted) type
// `convertOffsetStructTypeIgnoringDecorations` already gives a fixed-size
// array-of-matrix member.

// CHECK-LABEL: llvm.func @store_load
// CHECK-SAME: (%[[M:.*]]: !llvm.array<4 x vector<3xf32>>, %[[IDX:.*]]: i32) -> !llvm.array<4 x vector<3xf32>>
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (struct<"feme.tight_vector", (array<3 x f32>)>, i32, array<0 x array<3 x array<4 x f32>>>)>, 12, 1>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]]
// CHECK: %[[ELEM:.*]] = llvm.getelementptr inbounds %[[PTR]][0, %[[IDX]]]
// CHECK-SAME: -> !llvm.ptr<12>, !llvm.array<0 x array<3 x array<4 x f32>>>
//
// The value stored is transposed (RowMajor) into the physical (3-rows-of-
// 4-floats) shape before the real `llvm.store`, and the inverse happens
// after the real `llvm.load` -- both crossing exactly the same flat
// per-row type reached by `%[[ELEM]]` (no per-row padding needed here,
// since a 4-float row is already exactly the declared 16-byte
// `MatrixStride`).
// CHECK: llvm.store %{{.*}}, %[[ELEM]] : !llvm.array<3 x array<4 x f32>>, !llvm.ptr<12>
// CHECK: %[[LOADED:.*]] = llvm.load %[[ELEM]] : !llvm.ptr<12> -> !llvm.array<3 x array<4 x f32>>
// CHECK: llvm.return %{{.*}} : !llvm.array<4 x vector<3xf32>>
!Blk = !spirv.struct<(vector<3xf32> [0], si32 [12], !spirv.rtarray<!spirv.matrix<4 x vector<3xf32>>, stride=48> [16, RowMajor, MatrixStride=16]), BufferBlock>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ssbo bind(0, 2) : !spirv.ptr<!Blk, Uniform>
  spirv.func @store_load(%m : !spirv.matrix<4 x vector<3xf32>>, %idx : si32) -> !spirv.matrix<4 x vector<3xf32>> "None" {
    %addr = spirv.mlir.addressof @ssbo : !spirv.ptr<!Blk, Uniform>
    %c2 = spirv.Constant 2 : si32
    %melem = spirv.AccessChain %addr[%c2, %idx] : !spirv.ptr<!Blk, Uniform>, si32, si32 -> !spirv.ptr<!spirv.matrix<4 x vector<3xf32>>, Uniform>
    spirv.Store "Uniform" %melem, %m : !spirv.matrix<4 x vector<3xf32>>
    %v = spirv.Load "Uniform" %melem : !spirv.matrix<4 x vector<3xf32>>
    spirv.ReturnValue %v : !spirv.matrix<4 x vector<3xf32>>
  }
  spirv.EntryPoint "GLCompute" @store_load
  spirv.ExecutionMode @store_load "LocalSize", 1, 1, 1
}
