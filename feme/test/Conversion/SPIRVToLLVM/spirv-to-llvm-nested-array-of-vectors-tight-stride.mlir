// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap K6a (scalarBlockLayout): a single-level `!spirv.array<N x
// vector<3xT>>` member declared with a *tight* (scalar-layout) stride --
// `stride=12`, exactly a `vec3`'s own 3-element natural size, with no
// SIMD-register-driven rounding up to 16 -- used to fail the generic,
// registered `spirv::ArrayType` conversion outright
// (`convertArrayTypeIgnoringDecorations` returns null whenever a
// vector/matrix element's own natural ABI size exceeds the declared
// stride), aborting the *whole enclosing struct's* conversion before any
// of `convertOffsetStructTypeIgnoringDecorations`'s own later "tight"
// retry tiers ever ran -- `dEQP-VK.ubo.single_basic_array.scalar.vec3`'s
// own exact shape.
//
// Fixed by falling back to the new
// `getTightNestedArrayOfVectorsOrMatricesType` (which already exists to
// substitute an array-wrapped vector/matrix member for its own tight
// form inside the later retry tiers) directly from the struct
// member-list-building loop whenever the ordinary
// `TypeConverter::convertType` call on that member fails outright --
// this member is now a `feme.tight_vector`-marker-struct array from the
// very first, otherwise-unsubstituted attempt, with no retry needed at
// all.

// CHECK-LABEL: llvm.func @read
// CHECK: %{{.*}} = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (f32, array<3 x struct<"feme.tight_vector.f32x3", (array<3 x f32>)>>)>, 2, 0>
// CHECK: llvm.load %{{.*}} : !llvm.ptr<12> -> vector<3xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 2) : !spirv.ptr<!spirv.struct<(f32 [0], !spirv.array<3 x vector<3xf32>, stride=12> [4]), Block>, Uniform>
  spirv.func @read() -> f32 "None" {
    %0 = spirv.mlir.addressof @cb : !spirv.ptr<!spirv.struct<(f32 [0], !spirv.array<3 x vector<3xf32>, stride=12> [4]), Block>, Uniform>
    %c0 = spirv.Constant 0 : si32
    %c1 = spirv.Constant 1 : si32
    %c2 = spirv.Constant 0 : si32
    %ac = spirv.AccessChain %0[%c1, %c2] : !spirv.ptr<!spirv.struct<(f32 [0], !spirv.array<3 x vector<3xf32>, stride=12> [4]), Block>, Uniform>, si32, si32 -> !spirv.ptr<vector<3xf32>, Uniform>
    %v = spirv.Load "Uniform" %ac : vector<3xf32>
    %e = spirv.CompositeExtract %v[0 : i32] : vector<3xf32>
    spirv.ReturnValue %e : f32
  }
}
