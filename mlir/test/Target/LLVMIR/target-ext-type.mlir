// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

// CHECK: @global = global target("spirv.DeviceEvent") zeroinitializer
llvm.mlir.global external @global() {addr_space = 0 : i32} : !llvm.target<"spirv.DeviceEvent"> {
  %0 = llvm.mlir.zero : !llvm.target<"spirv.DeviceEvent">
  llvm.return %0 : !llvm.target<"spirv.DeviceEvent">
}

// CHECK: @amdgcn_named_barrier = internal addrspace(3) global target("amdgcn.named.barrier", 0) poison
llvm.mlir.global internal @amdgcn_named_barrier() {addr_space = 3 : i32} : !llvm.target<"amdgcn.named.barrier", 0> {
  %0 = llvm.mlir.poison : !llvm.target<"amdgcn.named.barrier", 0>
  llvm.return %0 : !llvm.target<"amdgcn.named.barrier", 0>
}

// CHECK: @image = external constant target("spirv.Image", float, 5, 0, 0, 0, 2, 1)
llvm.mlir.global external constant @image() {addr_space = 0 : i32} : !llvm.target<"spirv.Image", f32, 5, 0, 0, 0, 2, 1>

// CHECK-LABEL: define void @image_types
// CHECK-SAME:    (target("spirv.SignedImage", i32, 5, 0, 0, 0, 2, 24) %0,
// CHECK-SAME:     target("spirv.SampledImage", float, 1, 0, 0, 0, 1, 0) %1,
// CHECK-SAME:     target("spirv.Sampler") %2)
// CHECK:         load target("spirv.Image", float, 5, 0, 0, 0, 2, 1), ptr @image
llvm.func @image_types(%arg0: !llvm.target<"spirv.SignedImage", i32, 5, 0, 0, 0, 2, 24>,
                       %arg1: !llvm.target<"spirv.SampledImage", f32, 1, 0, 0, 0, 1, 0>,
                       %arg2: !llvm.target<"spirv.Sampler">) {
  %0 = llvm.mlir.addressof @image : !llvm.ptr
  %1 = llvm.load %0 : !llvm.ptr -> !llvm.target<"spirv.Image", f32, 5, 0, 0, 0, 2, 1>
  llvm.return
}

// CHECK-LABEL: define target("spirv.Event") @func2() {
// CHECK-NEXT:    %1 = alloca target("spirv.Event"), align 8
// CHECK-NEXT:    %2 = load target("spirv.Event"), ptr %1, align 8
// CHECK-NEXT:    ret target("spirv.Event") poison
llvm.func @func2() -> !llvm.target<"spirv.Event"> {
  %0 = llvm.mlir.constant(1 : i32) : i32
  %1 = llvm.mlir.poison : !llvm.target<"spirv.Event">
  %2 = llvm.alloca %0 x !llvm.target<"spirv.Event"> {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %3 = llvm.load %2 {alignment = 8 : i64} : !llvm.ptr -> !llvm.target<"spirv.Event">
  llvm.return %1 : !llvm.target<"spirv.Event">
}

// CHECK-LABEL: define void @func3() {
// CHECK-NEXT:    %1 = freeze target("spirv.DeviceEvent") zeroinitializer
// CHECK-NEXT:    ret void
llvm.func @func3() {
  %0 = llvm.mlir.zero : !llvm.target<"spirv.DeviceEvent">
  %1 = llvm.freeze %0 : !llvm.target<"spirv.DeviceEvent">
  llvm.return
}
