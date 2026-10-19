; RUN: llc -O0 -verify-machineinstrs -mtriple=spirv1.6-vulkan1.3-library %s -o - | FileCheck %s
; RUN: %if spirv-tools %{ llc -O0 -mtriple=spirv1.6-vulkan1.3-library %s -o - -filetype=obj | spirv-val %}

; Clang passes a resource's debug name to llvm.spv.resource.handlefrombinding as a
; pointer to a constant i8 string purely so the backend can read it at compile
; time and attach it as an OpName. That string must not be lowered into a real
; SPIR-V value: doing so needlessly requires the Int8 capability (the string's
; element type) for a shader that otherwise only uses 32-bit integers, and
; targets that do not support 8-bit integers would then fail to consume this
; module even though it has no genuine 8-bit arithmetic.

target datalayout = "e-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024-n8:16:32:64-G1"

@.str = private unnamed_addr constant [2 x i8] c"X\00", align 1
@.str.2 = private unnamed_addr constant [7 x i8] c"Result\00", align 1

; CHECK-NOT: OpCapability Int8
; CHECK-DAG: OpName {{%[0-9]+}} "X"
; CHECK-DAG: OpName {{%[0-9]+}} "Result"
; CHECK-NOT: OpName {{%[0-9]+}} ".str"
; CHECK-NOT: OpTypeInt 8

define void @main() local_unnamed_addr #0 {
entry:
  %X = tail call target("spirv.VulkanBuffer", [0 x i32], 12, 0) @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_a0i32_12_0t(i32 0, i32 0, i32 1, i32 0, ptr nonnull @.str)
  %Result = tail call target("spirv.VulkanBuffer", [0 x i32], 12, 1) @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_a0i32_12_1t(i32 0, i32 1, i32 1, i32 0, ptr nonnull @.str.2)

  %0 = tail call noundef nonnull align 4 dereferenceable(4) ptr addrspace(11) @llvm.spv.resource.getpointer.p11.tspirv.VulkanBuffer_a0i32_12_0t(target("spirv.VulkanBuffer", [0 x i32], 12, 0) %X, i32 0)
  %1 = load i32, ptr addrspace(11) %0, align 4
  %2 = add i32 %1, 1

  %3 = tail call noundef nonnull align 4 dereferenceable(4) ptr addrspace(11) @llvm.spv.resource.getpointer.p11.tspirv.VulkanBuffer_a0i32_12_1t(target("spirv.VulkanBuffer", [0 x i32], 12, 1) %Result, i32 0)
  store i32 %2, ptr addrspace(11) %3, align 4
  ret void
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(none)
declare ptr addrspace(11) @llvm.spv.resource.getpointer.p11.tspirv.VulkanBuffer_a0i32_12_0t(target("spirv.VulkanBuffer", [0 x i32], 12, 0), i32) #1

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(none)
declare ptr addrspace(11) @llvm.spv.resource.getpointer.p11.tspirv.VulkanBuffer_a0i32_12_1t(target("spirv.VulkanBuffer", [0 x i32], 12, 1), i32) #1

attributes #0 = { mustprogress nofree noinline norecurse nosync nounwind willreturn memory(readwrite, inaccessiblemem: none) "frame-pointer"="all" "hlsl.numthreads"="1,1,1" "hlsl.shader"="compute" }
attributes #1 = { mustprogress nocallback nofree nosync nounwind willreturn memory(none) }
