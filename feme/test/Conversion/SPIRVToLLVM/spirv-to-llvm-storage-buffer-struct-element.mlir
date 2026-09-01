// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap L19: `RWStructuredBuffer<T>`'s own per-element `T` (here `Doggo`)
// is reached, once `spirv.AccessChain` has already selected one array
// element, through an ordinary `StorageBuffer`-class struct pointer that is
// itself NOT `Block`-decorated -- only the outer wrapper struct
// (`type.RWStructuredBuffer.Doggo` below) carries that decoration. This
// used to be misclassified by `isBufferBlockStorage` (which returned true
// for *any* `StorageBuffer`-class struct pointer, `Block`-decorated or not)
// as itself a second, nested buffer block, producing a spurious
// `spirv.VulkanBuffer` handle type instead of ordinary memory -- caught by
// a real `Feature/StructuredBuffer/packed.test` reduction, whose own
// `Doggo Fido = Buf[GI]; ...; Buf[GI] = Fido;` whole-struct-copy idiom is
// the only shape that creates this kind of intermediate struct-typed
// pointer (every other `StructuredBuffer` test navigates directly to an
// individual scalar/vector field via a single multi-index access chain
// instead, never creating one).

// CHECK-LABEL: llvm.func @copy
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.array<0 x struct<(array<3 x i32>, i32)>>, 12, 1>
// CHECK: %[[ELEM:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK-SAME: -> !llvm.ptr<11>
// CHECK-NOT: !llvm.target<"spirv.VulkanBuffer"
// CHECK: %[[VAL:.*]] = llvm.load %[[ELEM]] : !llvm.ptr<11> -> !llvm.struct<(array<3 x i32>, i32)>
// CHECK: llvm.store %[[VAL]], %[[ELEM]] : !llvm.struct<(array<3 x i32>, i32)>, !llvm.ptr<11>
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @Buf bind(0, 0) : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.Doggo, (!spirv.rtarray<!spirv.struct<Doggo, (vector<3xsi32> [0], si32 [12])>, stride=16> [0]), Block>, StorageBuffer>
  spirv.func @copy(%idx : si32) -> () "None" {
    %addr = spirv.mlir.addressof @Buf : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.Doggo, (!spirv.rtarray<!spirv.struct<Doggo, (vector<3xsi32> [0], si32 [12])>, stride=16> [0]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : si32
    %elem = spirv.AccessChain %addr[%c0, %idx] : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.Doggo, (!spirv.rtarray<!spirv.struct<Doggo, (vector<3xsi32> [0], si32 [12])>, stride=16> [0]), Block>, StorageBuffer>, si32, si32 -> !spirv.ptr<!spirv.struct<Doggo, (vector<3xsi32> [0], si32 [12])>, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %elem : !spirv.struct<Doggo, (vector<3xsi32> [0], si32 [12])>
    spirv.Store "StorageBuffer" %elem, %v : !spirv.struct<Doggo, (vector<3xsi32> [0], si32 [12])>
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @copy
  spirv.ExecutionMode @copy "LocalSize", 2, 1, 1
}
