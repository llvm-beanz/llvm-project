// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L289: a uniform block member that is itself a nested struct
// containing an *array of vectors* (e.g. GLSL's
// `struct T { float a; vec2 b[2]; };`, nested inside an enclosing
// `struct S { float a; T b[3]; int c; };` array), the shape
// `dEQP-VK.glsl.struct.uniform.nested_struct_array_fragment` exercises.
//
// `getTightNestedStructType` wraps `T::b`'s own `vec2` array elements in
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
// first. Fixed by having the array branch track and consult the same
// kind of "real field type" hint the struct branch already computes for
// a direct vector member, keyed off this array's own declared element
// type instead.

// CHECK-LABEL: llvm.func @nested_struct_array_vector_component
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]]
// CHECK: %[[GEP:.*]] = llvm.getelementptr inbounds %[[PTR]][0, 2, %{{.*}}, 2, %{{.*}}, 0, %{{.*}}]
// CHECK-SAME: !llvm.struct<packed (f32, array<12 x i8>, array<3 x struct<packed (f32, array<12 x i8>, array<2 x struct<"feme.tight_vector.f32x2", (array<2 x f32>)>>, array<16 x i8>)>>, i32)>
// CHECK: llvm.load %[[GEP]]

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @block bind(0, 0) : !spirv.ptr<!spirv.struct<type.Block, (!spirv.array<2 x !spirv.struct<S, (f32 [0], !spirv.array<3 x !spirv.struct<T, (f32 [0], !spirv.array<2 x vector<2xf32>, stride=16> [16])>, stride=48> [16], si32 [160])>, stride=176> [0]), Block>, Uniform>
  spirv.func @nested_struct_array_vector_component() -> () "None" {
    %addr = spirv.mlir.addressof @block : !spirv.ptr<!spirv.struct<type.Block, (!spirv.array<2 x !spirv.struct<S, (f32 [0], !spirv.array<3 x !spirv.struct<T, (f32 [0], !spirv.array<2 x vector<2xf32>, stride=16> [16])>, stride=48> [16], si32 [160])>, stride=176> [0]), Block>, Uniform>
    %c0 = spirv.Constant 0 : si32
    %c1 = spirv.Constant 1 : si32
    // s[0].b[1].b[1].x
    %ac = spirv.AccessChain %addr[%c0, %c0, %c1, %c1, %c1, %c0, %c0] : !spirv.ptr<!spirv.struct<type.Block, (!spirv.array<2 x !spirv.struct<S, (f32 [0], !spirv.array<3 x !spirv.struct<T, (f32 [0], !spirv.array<2 x vector<2xf32>, stride=16> [16])>, stride=48> [16], si32 [160])>, stride=176> [0]), Block>, Uniform>, si32, si32, si32, si32, si32, si32, si32 -> !spirv.ptr<f32, Uniform>
    %v = spirv.Load "Uniform" %ac : f32
    spirv.Return
  }
}
