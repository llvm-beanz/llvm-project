; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Covers feme::cpu::WaveLoweringPass's lowering of the roadmap step R4
; `feme.cpu.wave.*` reduce/scan kinds (lowerActiveReduce/lowerPrefixReduce
; in WaveLowering.cpp): `WaveActiveSum`/`Max`/`BitAnd` reduce to
; `llvm.vector.reduce.*` over `select(M, X, identity)` per "Phase 5"'s
; table, and `WavePrefixSum`/`PrefixProduct` scan the same masked operand
; lane by lane, matching `WavePrefixCountBits`'s existing lane-loop shape.
; `test/Tools/feme-run/HLSL/prefix-sum.hlsl` exercises `WavePrefixSum`
; through the full CPU pipeline; this test isolates the lowering itself.

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.wave

define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)

  ; CHECK: %[[SUMSEL:.*]] = select <4 x i1> %wave_entry_mask, <4 x i32> %{{.*}}, <4 x i32> zeroinitializer
  ; CHECK: call i32 @llvm.vector.reduce.add.v4i32(<4 x i32> %[[SUMSEL]])
  %s = call i32 @llvm.dx.wave.reduce.sum.i32(i32 %tid)

  %tidf = uitofp i32 %tid to float
  ; CHECK: %[[MAXSEL:.*]] = select <4 x i1> %wave_entry_mask, <4 x float> %{{.*}}, <4 x float> splat (float -inf)
  ; CHECK: call float @llvm.vector.reduce.fmax.v4f32(<4 x float> %[[MAXSEL]])
  %m = call float @llvm.dx.wave.reduce.max.f32(float %tidf)

  ; CHECK: %[[ANDSEL:.*]] = select <4 x i1> %wave_entry_mask, <4 x i32> %{{.*}}, <4 x i32> splat (i32 -1)
  ; CHECK: call i32 @llvm.vector.reduce.and.v4i32(<4 x i32> %[[ANDSEL]])
  %a = call i32 @llvm.dx.wave.reduce.and.i32(i32 %tid)

  ; CHECK: %[[PSEL:.*]] = select <4 x i1> %wave_entry_mask, <4 x i32> %{{.*}}, <4 x i32> zeroinitializer
  ; CHECK: %[[E0:.*]] = extractelement <4 x i32> %[[PSEL]], i32 0
  ; CHECK: %[[A0:.*]] = add i32 0, %[[E0]]
  ; CHECK: insertelement <4 x i32> <i32 0, i32 poison, i32 poison, i32 poison>, i32 %[[A0]], i32 1
  %p = call i32 @llvm.dx.wave.prefix.sum.i32(i32 %tid)

  ; CHECK: %[[PPSEL:.*]] = select <4 x i1> %wave_entry_mask, <4 x float> %{{.*}}, <4 x float> splat (float 1.000000e+00)
  ; CHECK: %[[PE0:.*]] = extractelement <4 x float> %[[PPSEL]], i32 0
  ; CHECK: %[[PM0:.*]] = fmul float 1.000000e+00, %[[PE0]]
  ; CHECK: insertelement <4 x float> <float 1.000000e+00, float poison, float poison, float poison>, float %[[PM0]], i32 1
  %pp = call float @llvm.dx.wave.prefix.product.f32(float %tidf)

  ret void
}

declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.wave.reduce.sum.i32(i32)
declare float @llvm.dx.wave.reduce.max.f32(float)
declare i32 @llvm.dx.wave.reduce.and.i32(i32)
declare i32 @llvm.dx.wave.prefix.sum.i32(i32)
declare float @llvm.dx.wave.prefix.product.f32(float)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
