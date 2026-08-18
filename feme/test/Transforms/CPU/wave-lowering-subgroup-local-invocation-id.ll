; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; V4: llvm.spv.subgroup.local.invocation.id (Vulkan's
; SubgroupLocalInvocationId builtin) lowers to the constant lane iota
; directly, with no group id/wave index arithmetic at all -- exactly like
; llvm.dx.wave.getlaneindex (see SIMDize.cpp's classifyBuiltin comment for
; why they share BuiltinCallKind::LaneIndex).

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.builtin
; CHECK: ret void
define void @main() #0 {
  %lane = call i32 @llvm.spv.subgroup.local.invocation.id()
  %doubled = mul i32 %lane, 2
  ret void
}
declare i32 @llvm.spv.subgroup.local.invocation.id()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
