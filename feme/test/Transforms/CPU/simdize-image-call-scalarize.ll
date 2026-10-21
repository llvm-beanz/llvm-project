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

; `main` is a `Compute`-stage entry point, so -- since `VK_KHR_compute_
; shader_derivatives`'s own fix (roadmap ENV5(b)(2), widening `ImageCalls.
; cpp`'s derivative-synthesis gate from Fragment-only to Fragment-or-
; Compute) -- its divergent sample's four derivative operands (ddx.x,
; ddx.y, ddy.x, ddy.y) are now real per-lane `feme.stage.derivative.*`
; values rather than the zero constants this test originally hardcoded;
; only the shape of the scalarized call (not each operand's exact value)
; is this test's own concern, so those four are now wildcards too.
; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <4 x float>>
; CHECK-COUNT-4: call <4 x float> @feme.cpu.image.sample.2d.v4f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float {{%.*}}, float 0.000000e+00, float {{%.*}}, float {{%.*}}, float {{%.*}}, float {{%.*}}, float 0.000000e+00, i1 false, float 0.000000e+00, i32 0, i32 0, float -inf, i1 {{%.*}})
; CHECK: fadd <4 x float>
define void @main() #0 {
  %img = call target("dx.Texture", <4 x float>, 0, 0, 0, 2)
      @llvm.dx.resource.handlefromheap.timg(i32 0)
  %samp = call target("dx.Sampler", 0)
      @llvm.dx.resource.handlefromheap.tsamp(i32 0)
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
    @llvm.dx.resource.handlefromheap.timg(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tsamp(i32)
declare <4 x float> @llvm.dx.resource.sample.v4f32.timg.tsamp.v2f32(
    target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0),
    <2 x float>, <2 x i32>)
declare i32 @llvm.dx.thread.id(i32)
declare <4 x float> @llvm.dx.resource.samplelevel.v4f32.timg.tsamp.v2f32(
    target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0),
    <2 x float>, float, <2 x i32>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }

; An explicit-LOD sample never synthesizes derivatives at all (its own LOD
; is a direct operand, not computed from screen-space coordinate spread),
; so every lane performs identically regardless of stage -- this was
; already correct before the Compute-stage derivative-synthesis fix above
; and stays a single scalar call: there is nothing to widen. (An implicit-
; LOD `Sample` would no longer fit this shape in a `Compute` entry point,
; since -- after that fix -- it legitimately synthesizes a real, lane-
; position-dependent derivative, which this pass must scalarize per lane
; like any other divergent operand.)

; CHECK-LABEL: define void @uniform_sample(
; CHECK: call <4 x float> @feme.cpu.image.sample.2d.v4f32(
; CHECK-NOT: call <4 x float> @feme.cpu.image.sample.2d.v4f32(
define void @uniform_sample() #0 {
  %img = call target("dx.Texture", <4 x float>, 0, 0, 0, 2)
      @llvm.dx.resource.handlefromheap.timg(i32 0)
  %samp = call target("dx.Sampler", 0)
      @llvm.dx.resource.handlefromheap.tsamp(i32 0)
  %texel = call <4 x float> @llvm.dx.resource.samplelevel.v4f32.timg.tsamp.v2f32(
      target("dx.Texture", <4 x float>, 0, 0, 0, 2) %img,
      target("dx.Sampler", 0) %samp,
      <2 x float> <float 0.5, float 0.5>, float 0.0, <2 x i32> zeroinitializer)
  %e0 = extractelement <4 x float> %texel, i32 0
  ret void
}
