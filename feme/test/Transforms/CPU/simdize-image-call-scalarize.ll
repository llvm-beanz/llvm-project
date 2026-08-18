; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Closes roadmap R30's "active-lane SIMD widening for a *divergent* sample"
; gap. `feme.cpu.image.*` does not fit `MatchedResourceCall`'s fixed
; (heap, index, offset, [value], mask) shape -- it carries two heaps, two
; descriptor indices and several coordinate operands (see ImageCalls.h) --
; so `feme::cpu::SIMDizePass` used to leave a divergent sample as a single
; scalar call fed a widened, wrongly-typed coordinate. It now scalarizes it
; per lane exactly like a divergent buffer access, decomposing the
; `<4 x float>` result into one `<4 x float>` per *component* rather than an
; illegal `<4 x <4 x float>>` ("Vectors become components, not nested
; vectors" in "Phase 4: Widening").

; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <4 x float>>
; CHECK-COUNT-4: call <4 x float> @feme.cpu.image.sample.2d.v4f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float {{%.*}}, float 0.000000e+00, float 0.000000e+00, i1 false, i1 {{%.*}})
; CHECK: fadd <4 x float>
define void @main() #0 {
  %img = call target("dx.Texture", <4 x float>, 0, 0, 0, 2)
      @llvm.dx.resource.handlefromheap.timg(i32 0, i1 false)
  %samp = call target("dx.Sampler", 0)
      @llvm.dx.resource.handlefromheap.tsamp(i32 0, i1 false)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %u = sitofp i32 %tid to float
  %coord = insertelement <2 x float> <float 0.0, float 0.0>, float %u, i32 0
  %texel = call <4 x float> @llvm.dx.resource.sample.v4f32.timg.tsamp.v2f32(
      target("dx.Texture", <4 x float>, 0, 0, 0, 2) %img,
      target("dx.Sampler", 0) %samp, <2 x float> %coord,
      <2 x i32> zeroinitializer)
  %e0 = extractelement <4 x float> %texel, i32 0
  %e2 = extractelement <4 x float> %texel, i32 2
  %sum = fadd float %e0, %e2
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 2)
    @llvm.dx.resource.handlefromheap.timg(i32, i1)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tsamp(i32, i1)
declare <4 x float> @llvm.dx.resource.sample.v4f32.timg.tsamp.v2f32(
    target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0),
    <2 x float>, <2 x i32>)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }

; A sample every lane performs identically -- the common compute-shader case
; -- was already correct before this and stays a single scalar call: there
; is nothing to widen.

; CHECK-LABEL: define void @uniform_sample(
; CHECK: call <4 x float> @feme.cpu.image.sample.2d.v4f32(
; CHECK-NOT: call <4 x float> @feme.cpu.image.sample.2d.v4f32(
define void @uniform_sample() #0 {
  %img = call target("dx.Texture", <4 x float>, 0, 0, 0, 2)
      @llvm.dx.resource.handlefromheap.timg(i32 0, i1 false)
  %samp = call target("dx.Sampler", 0)
      @llvm.dx.resource.handlefromheap.tsamp(i32 0, i1 false)
  %texel = call <4 x float> @llvm.dx.resource.sample.v4f32.timg.tsamp.v2f32(
      target("dx.Texture", <4 x float>, 0, 0, 0, 2) %img,
      target("dx.Sampler", 0) %samp,
      <2 x float> <float 0.5, float 0.5>, <2 x i32> zeroinitializer)
  %e0 = extractelement <4 x float> %texel, i32 0
  ret void
}
