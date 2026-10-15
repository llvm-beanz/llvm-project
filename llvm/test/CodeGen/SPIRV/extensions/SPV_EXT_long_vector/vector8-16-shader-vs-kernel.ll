; The Vector16 capability (used to express native 8- and 16-component
; OpTypeVector types) requires the Kernel capability per the SPIR-V spec, so
; it cannot be used under the Shader execution model (e.g. Vulkan). Under
; Shader, widths 8 and 16 must instead go through SPV_EXT_long_vector like any
; other non-native width.

; RUN: llc -verify-machineinstrs -O0 -mtriple=spirv64-unknown-unknown %s -o - | FileCheck %s --check-prefix=KERNEL
; RUN: %if spirv-tools %{ llc -O0 -mtriple=spirv64-unknown-unknown %s -o - -filetype=obj | spirv-val %}

; RUN: llc -verify-machineinstrs -O0 -mtriple=spirv-unknown-vulkan1.3 --spirv-ext=+SPV_EXT_long_vector %s -o - | FileCheck %s --check-prefix=SHADER
; RUN: %if spirv-tools %{ llc -O0 -mtriple=spirv-unknown-vulkan1.3 --spirv-ext=+SPV_EXT_long_vector %s -o - -filetype=obj | spirv-val --target-env vulkan1.3 %}

; KERNEL-DAG: OpCapability Kernel
; KERNEL-DAG: OpCapability Vector16
; KERNEL-NOT: OpCapability LongVectorEXT
; KERNEL-NOT: OpExtension "SPV_EXT_long_vector"
; KERNEL-DAG: %[[#I32:]] = OpTypeInt 32 0
; KERNEL-DAG: %[[#V8:]] = OpTypeVector %[[#I32]] 8
; KERNEL-DAG: %[[#V16:]] = OpTypeVector %[[#I32]] 16

; SHADER-NOT: OpCapability Kernel
; SHADER-NOT: OpCapability Vector16
; SHADER-DAG: OpCapability LongVectorEXT
; SHADER-DAG: OpExtension "SPV_EXT_long_vector"
; SHADER-DAG: %[[#I32:]] = OpTypeInt 32 0
; SHADER-DAG: %[[#EIGHT:]] = OpConstant %[[#I32]] 8
; SHADER-DAG: %[[#SIXTEEN:]] = OpConstant %[[#I32]] 16
; SHADER-DAG: %[[#V8:]] = OpTypeVectorIdEXT %[[#I32]] %[[#EIGHT]]
; SHADER-DAG: %[[#V16:]] = OpTypeVectorIdEXT %[[#I32]] %[[#SIXTEEN]]

define <8 x i32> @add_v8(<8 x i32> %a, <8 x i32> %b) {
  %r = add <8 x i32> %a, %b
  ret <8 x i32> %r
}

define <16 x i32> @add_v16(<16 x i32> %a, <16 x i32> %b) {
  %r = add <16 x i32> %a, %b
  ret <16 x i32> %r
}
