; RUN: llc -mtriple=spirv-unknown-vulkan1.3 %s -o - | FileCheck %s
; RUN: %if spirv-tools %{ llc -mtriple=spirv-unknown-vulkan1.3 %s -filetype=obj -o - | spirv-val --target-env vulkan1.3 %}

; CHECK: OpCapability SampleRateShading
; CHECK-DAG: OpName %[[#INPUT:]] "input"
; CHECK-DAG: OpDecorate %[[#INPUT]] Location 0
; CHECK-DAG: OpDecorate %[[#INPUT]] Sample

@input = external hidden addrspace(7) constant float, !spirv.Decorations !0
@output = external hidden addrspace(8) global float, !spirv.Decorations !1

define void @main() #0 {
  %value = load float, ptr addrspace(7) @input
  store float %value, ptr addrspace(8) @output
  ret void
}

attributes #0 = { "hlsl.shader"="pixel" }

!0 = !{!2, !3}
!1 = !{!2}
!2 = !{i32 30, i32 0}
!3 = !{i32 17}
