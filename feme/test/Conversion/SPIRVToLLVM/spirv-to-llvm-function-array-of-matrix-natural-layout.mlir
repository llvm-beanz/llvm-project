// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// A `Function`-local array of `mat3` is declared with the natural
// (16-byte-column) layout, so loading/storing one element through an
// `AccessChain` must use that same layout rather than the tightened
// 12-byte-column one reserved for struct members.

// CHECK-LABEL: llvm.func @load_store_element
// CHECK: llvm.alloca %{{.*}} x !llvm.array<3 x array<3 x vector<3xf32>>>
// CHECK-NOT: feme.tight_vector
// CHECK: llvm.load %{{.*}} : !llvm.ptr -> !llvm.array<3 x vector<3xf32>>
// CHECK-NOT: feme.tight_vector
// CHECK: llvm.store %{{.*}}, %{{.*}} : !llvm.array<3 x vector<3xf32>>, !llvm.ptr
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @load_store_element() -> !spirv.matrix<3 x vector<3xf32>> "None" {
    %c1 = spirv.Constant 1 : i32
    %c2 = spirv.Constant 2 : i32
    %a = spirv.Variable : !spirv.ptr<!spirv.array<3 x !spirv.matrix<3 x vector<3xf32>>>, Function>
    %p = spirv.AccessChain %a[%c1] : !spirv.ptr<!spirv.array<3 x !spirv.matrix<3 x vector<3xf32>>>, Function>, i32 -> !spirv.ptr<!spirv.matrix<3 x vector<3xf32>>, Function>
    %m = spirv.Load "Function" %p : !spirv.matrix<3 x vector<3xf32>>
    %q = spirv.AccessChain %a[%c2] : !spirv.ptr<!spirv.array<3 x !spirv.matrix<3 x vector<3xf32>>>, Function>, i32 -> !spirv.ptr<!spirv.matrix<3 x vector<3xf32>>, Function>
    spirv.Store "Function" %q, %m : !spirv.matrix<3 x vector<3xf32>>
    spirv.ReturnValue %m : !spirv.matrix<3 x vector<3xf32>>
  }
}
