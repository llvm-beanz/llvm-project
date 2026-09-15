; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H124a: a vector-operand `WaveActive*` reduce call
; (`GroupNonUniformReducePattern`, SPIRVToLLVMPatterns.cpp, converts a
; vector-operand `spirv.GroupNonUniform*` reduce directly to the matching
; `llvm.spv.wave.*` intrinsic's own vector-typed overload, unchanged, since
; SPIR-V defines every one of these reductions component-wise) decomposes
; into one scalar `feme.cpu.wave.active_sum` reduce per component here,
; mirroring `FunctionWidener::widenWaveCall`'s existing `AllEqual`/
; `ReadLane` vector branches -- unlike those two, whose folded/gathered
; result needs extra reassembly, every `WaveActive*` reduce kind is
; unconditionally uniform (`isDivergentWaveCallResult`), so each
; component's own scalar reduce result is inserted directly into the
; result vector.

; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <2 x i32>>
; CHECK: %[[S0:.*]] = call i32 @feme.cpu.wave.active_sum.i32.v4(<4 x i1> %wave_entry_mask, <4 x i32> %{{.*}})
; CHECK: insertelement <2 x i32> poison, i32 %[[S0]], i32 0
; CHECK: %[[S1:.*]] = call i32 @feme.cpu.wave.active_sum.i32.v4(<4 x i1> %wave_entry_mask, <4 x i32> %{{.*}})
; CHECK: insertelement <2 x i32> %{{.*}}, i32 %[[S1]], i32 1
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %v0 = insertelement <2 x i32> poison, i32 %tid, i32 0
  %v1 = insertelement <2 x i32> %v0, i32 %tid, i32 1
  %reduced = call <2 x i32> @llvm.spv.wave.reduce.sum.v2i32(<2 x i32> %v1)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare <2 x i32> @llvm.spv.wave.reduce.sum.v2i32(<2 x i32>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
