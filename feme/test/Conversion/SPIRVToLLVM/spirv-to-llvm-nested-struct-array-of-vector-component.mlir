// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L289/L290: a uniform block member that is itself a nested
// struct containing an *array of vectors* (e.g. GLSL's
// `struct T { float a; vec2 b[2]; };`, nested inside an enclosing
// `struct S { float a; T b[3]; int c; };` array), the shape
// `dEQP-VK.glsl.struct.uniform.nested_struct_array_fragment` exercises.
// Covers two distinct bugs this shape found, both now fixed:
//
// (L289) `convertOffsetStructTypeIgnoringDecorations`'s array-of-vectors
// struct-member retry tier wraps `T::b`'s own `vec2` array elements in
// `getTightVectorArrayType`'s marker struct uniformly (any vector nested
// inside an array, regardless of lane count), so a component-selecting
// access chain into `T::b[i]` needs one extra `0` index to cross that
// marker before reaching the vector's own packed-array stand-in --
// `remapNestedStructMemberIndices`'s struct-member branch already
// inserted that extra index for a vector reached *directly* as a struct
// member (roadmap H101j), but its array branch did not insert the
// equivalent extra index for a vector reached one level further, through
// an array member -- `llvm.getelementptr`'s own verifier rejected the
// result outright ("index N indexing a struct is out of bounds"), since
// the final (component-selecting) index landed directly on the
// single-member marker struct itself instead of unwrapping into it
// first. A single access chain alone did not reproduce this (confirmed
// during triage); reproducing it required two differently-shaped access
// chains into the same block, as below. Fixed by having the array branch
// track and consult the same kind of "real field type" hint the struct
// branch already computes for a direct vector member, keyed off this
// array's own declared element type instead.
//
// (L290) Even once L289's GEP-index bug was fixed, the marker struct
// substituted for each `vec2 b[2]` element was only 8 bytes (an
// `array<2 x f32>` with no internal padding), disagreeing with `b`'s own
// declared 16-byte `ArrayStride` -- so `b[1]` would be read/written 8
// bytes short of its real, spec-required offset (a silent "Image
// mismatch" at runtime, not a crash, since the address stays within `T`'s
// own overall footprint). Fixed by baking the needed trailing padding
// directly into the marker struct itself (see
// `getOrCreateTightVectorMarkerStruct`'s own `TrailingPaddingBytes`
// parameter and `getStridedTightVectorArrayType`), since the existing
// `padStructToSize` helper explicitly refuses to touch a tight-vector
// marker struct, by design.

// CHECK-LABEL: llvm.func @nested_struct_array_vector_component
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[PTR0:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]]
// CHECK: %[[GEP0:.*]] = llvm.getelementptr inbounds %[[PTR0]][0, 2, %{{.*}}, 2, %{{.*}}, 0, %{{.*}}]
// CHECK-SAME: !llvm.struct<packed (f32, array<12 x i8>, array<3 x struct<packed (f32, array<12 x i8>, array<2 x struct<"feme.tight_vector.f32x2.pad8", (array<2 x f32>, array<8 x i8>)>>)>>, i32)>
// CHECK: llvm.load %[[GEP0]]
// CHECK: %[[PTR1:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]]
// CHECK: %[[GEP1:.*]] = llvm.getelementptr inbounds %[[PTR1]][0, 2, %{{.*}}, 2, %{{.*}}, 0, %{{.*}}]
// CHECK-SAME: !llvm.struct<packed (f32, array<12 x i8>, array<3 x struct<packed (f32, array<12 x i8>, array<2 x struct<"feme.tight_vector.f32x2.pad8", (array<2 x f32>, array<8 x i8>)>>)>>, i32)>
// CHECK: llvm.load %[[GEP1]]

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @block bind(0, 0) : !spirv.ptr<!spirv.struct<type.Block, (!spirv.array<2 x !spirv.struct<S, (f32 [0], !spirv.array<3 x !spirv.struct<T, (f32 [0], !spirv.array<2 x vector<2xf32>, stride=16> [16])>, stride=48> [16], si32 [160])>, stride=176> [0]), Block>, Uniform>
  spirv.func @nested_struct_array_vector_component() -> () "None" {
    %addr = spirv.mlir.addressof @block : !spirv.ptr<!spirv.struct<type.Block, (!spirv.array<2 x !spirv.struct<S, (f32 [0], !spirv.array<3 x !spirv.struct<T, (f32 [0], !spirv.array<2 x vector<2xf32>, stride=16> [16])>, stride=48> [16], si32 [160])>, stride=176> [0]), Block>, Uniform>
    %c0 = spirv.Constant 0 : si32
    %c1 = spirv.Constant 1 : si32
    // s[0].b[1].b[1].x
    %ac0 = spirv.AccessChain %addr[%c0, %c0, %c1, %c1, %c1, %c0, %c0] : !spirv.ptr<!spirv.struct<type.Block, (!spirv.array<2 x !spirv.struct<S, (f32 [0], !spirv.array<3 x !spirv.struct<T, (f32 [0], !spirv.array<2 x vector<2xf32>, stride=16> [16])>, stride=48> [16], si32 [160])>, stride=176> [0]), Block>, Uniform>, si32, si32, si32, si32, si32, si32, si32 -> !spirv.ptr<f32, Uniform>
    %v0 = spirv.Load "Uniform" %ac0 : f32
    // s[1].b[2].b[1].y
    %c2 = spirv.Constant 2 : si32
    %ac1 = spirv.AccessChain %addr[%c0, %c1, %c1, %c2, %c1, %c1, %c1] : !spirv.ptr<!spirv.struct<type.Block, (!spirv.array<2 x !spirv.struct<S, (f32 [0], !spirv.array<3 x !spirv.struct<T, (f32 [0], !spirv.array<2 x vector<2xf32>, stride=16> [16])>, stride=48> [16], si32 [160])>, stride=176> [0]), Block>, Uniform>, si32, si32, si32, si32, si32, si32, si32 -> !spirv.ptr<f32, Uniform>
    %v1 = spirv.Load "Uniform" %ac1 : f32
    spirv.Return
  }
}
