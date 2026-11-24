; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define float @missing_layer(
; CHECK-NOT: @feme.stage.derivative
; CHECK: call float @llvm.dx.resource.sample.
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @dynamic_offset(
; CHECK-NOT: @feme.stage.derivative
; CHECK: call float @llvm.dx.resource.sample.
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @poison_offset(
; CHECK-NOT: @feme.stage.derivative
; CHECK: call float @llvm.dx.resource.sample.
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @compute(
; CHECK-NOT: @feme.stage.derivative
; CHECK: call float @llvm.dx.resource.sample.
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define <4 x float> @too_wide(
; CHECK-NOT: @feme.stage.derivative
; CHECK: call <4 x float> @llvm.dx.resource.sample.
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @plain(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[DX:%.*]] = call float @feme.stage.derivative.x.coarse.f32(float %coord)
; CHECK: [[DY:%.*]] = call float @feme.stage.derivative.y.coarse.f32(float %coord)
; CHECK-NOT: @feme.stage.derivative
; CHECK: [[P:%.*]] = call <4 x float> @feme.cpu.image.sample.1d.v4f32(
; CHECK-SAME: i32 %image, i32 %sampler, float %coord, float [[DX]], float [[DY]], float 0.000000e+00, i1 false, float 0.000000e+00, i32 -1, float -inf, i1 true)
; CHECK: extractelement <4 x float> [[P]], i64 0
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <2 x float> @array(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[U:%.*]] = extractelement <2 x float> %coord, i64 0
; CHECK: [[AX:%.*]] = call float @feme.stage.derivative.x.coarse.f32(float [[U]])
; CHECK: [[AY:%.*]] = call float @feme.stage.derivative.y.coarse.f32(float [[U]])
; CHECK: [[L:%.*]] = extractelement <2 x float> %coord, i64 1
; CHECK-NOT: @feme.stage.derivative
; CHECK: [[A:%.*]] = call <4 x float> @feme.cpu.image.sample.1darray.v4f32(
; CHECK-SAME: float [[U]], float [[L]], float [[AX]], float [[AY]], float 0.000000e+00, i1 false, float 0.000000e+00, i32 1, float -inf, i1 true)
; CHECK: shufflevector <4 x float> [[A]], <4 x float> poison, <2 x i32> <i32 0, i32 1>
; CHECK-NOT: call target("dx.
define float @plain(i32 %image, i32 %sampler, float %coord) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 %image)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 %sampler)
  %r = call float @llvm.dx.resource.sample.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float %coord, i32 -1)
  ret float %r
}
define <2 x float> @array(<2 x float> %coord) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <2 x float> @llvm.dx.resource.sample.v2f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, i32 1)
  ret <2 x float> %r
}
define float @missing_layer(float %coord) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.sample.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t, target("dx.Sampler", 0) %s, float %coord, i32 0)
  ret float %r
}
define float @dynamic_offset(i32 %offset) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.sample.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float 0.0, i32 %offset)
  ret float %r
}
define float @poison_offset() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.sample.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, i32 poison)
  ret float %r
}
define float @compute(float %coord) #1 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.sample.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float %coord, i32 0)
  ret float %r
}
define <4 x float> @too_wide() #0 {
  %t = call target("dx.Texture", float, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_1t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.sample.v4f32.tdx.Texture_f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", float, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float 0.0, i32 0)
  ret <4 x float> %r
}
; CHECK: !feme.cpu.resources = !{![[PLAIN:[0-9]+]], ![[ARRAY:[0-9]+]]}
; CHECK: ![[PLAIN]] = !{!"plain", i32 0, i1 true
; CHECK: ![[ARRAY]] = !{!"array", i32 0, i1 true
declare target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32)
declare target("dx.Texture", float, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_1t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare float @llvm.dx.resource.sample.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), target("dx.Sampler", 0), float, i32)
declare <2 x float> @llvm.dx.resource.sample.v2f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), <2 x float>, i32)
declare float @llvm.dx.resource.sample.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), float, i32)
declare float @llvm.dx.resource.sample.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), <2 x float>, i32)
declare <4 x float> @llvm.dx.resource.sample.v4f32.tdx.Texture_f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", float, 0, 0, 0, 1), target("dx.Sampler", 0), float, i32)
attributes #0 = { "feme.shader.stage"="fragment" }
attributes #1 = { "feme.shader.stage"="compute" }
