; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H7o: an ordinary, non-groupshared divergent-address `load` of
; vector type -- the "local constant lookup table indexed by a
; per-invocation builtin" shape, reduced from a real
; `dEQP-VK.pipeline.monolithic.multisample.min_sample_shading_*` vertex
; shader's own `positions[gl_VertexIndex]` (glslang's usual
; `const vec4 positions[4] = vec4[4](...); gl_Position =
; positions[gl_VertexIndex];` idiom) -- used to hit
; `checkVectorDecompositionSupported`'s "has a divergent value of vector
; type" diagnostic unconditionally: no producer case covered a `LoadInst`
; at all, unlike a divergent-address load of *scalar* type, which
; `widenScalarizedFallback` already scalarized correctly (see
; `simdize-groupshared-divergent-index.ll` for the groupshared,
; gather-based analogue of the same address-divergence). A `LoadInst` is
; now a supported vector-typed producer, decomposed into 4 widened
; per-component values by `widenScalarizedFallback`'s per-lane clone (one
; real load through each lane's own extracted address) instead of trying
; to build one illegal `<4 x <4 x float>>` result.

; CHECK-LABEL: define void @main(
; CHECK-COUNT-4: getelementptr [4 x <4 x float>], ptr %{{.*}}, i32 0, i32 %{{.*}}
; CHECK-COUNT-4: load <4 x float>, ptr %{{.*}}, align 4
define void @main() #0 {
  %table = alloca [4 x <4 x float>], align 4
  store [4 x <4 x float>] [
      <4 x float> <float -1.0, float -1.0, float 0.0, float 1.0>,
      <4 x float> <float -1.0, float  1.0, float 0.0, float 1.0>,
      <4 x float> <float  1.0, float -1.0, float 0.0, float 1.0>,
      <4 x float> <float  1.0, float  1.0, float 0.0, float 1.0>],
      ptr %table, align 4
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %elt = getelementptr [4 x <4 x float>], ptr %table, i32 0, i32 %tid
  %v = load <4 x float>, ptr %elt, align 4
  %x = extractelement <4 x float> %v, i64 0
  store float %x, ptr @out
  ret void
}
@out = internal global float 0.0
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
