; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H127: a vector-operand `WavePrefixSum`/`WavePrefixProduct` call
; (`GroupNonUniformScanPattern`, SPIRVToLLVMPatterns.cpp, H126, converts a
; vector-operand `spirv.GroupNonUniform*` exclusive scan directly to the
; matching `llvm.spv.wave.prefix.*` intrinsic's own vector-typed overload,
; unchanged, since SPIR-V defines every one of these scans component-wise)
; decomposes into one `feme.cpu.wave.prefix_sum` scan per component here,
; mirroring `isVectorOperandReduceKind`'s sibling branch in
; `FunctionWidener::widenWaveCall` -- but unlike a reduce (whose
; per-component result is a single uniform scalar, insertable directly into
; a result vector), a prefix scan's result is always divergent per-lane
; (`isDivergentWaveCallResult`), so each component's own wide `<4 x i32>`
; result stays decomposed for its `extractelement` users, exactly like
; `ReadLane`'s own always-divergent sibling case.

; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <2 x i32>>
; CHECK: %[[S0:.*]] = call <4 x i32> @feme.cpu.wave.prefix_sum.i32.v4(<4 x i1> %wave_entry_mask, <4 x i32> %{{.*}})
; CHECK: %[[S1:.*]] = call <4 x i32> @feme.cpu.wave.prefix_sum.i32.v4(<4 x i1> %wave_entry_mask, <4 x i32> %{{.*}})
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %v0 = insertelement <2 x i32> poison, i32 %tid, i32 0
  %v1 = insertelement <2 x i32> %v0, i32 %tid, i32 1
  %scanned = call <2 x i32> @llvm.spv.wave.prefix.sum.v2i32(<2 x i32> %v1)
  %e0 = extractelement <2 x i32> %scanned, i32 0
  %e1 = extractelement <2 x i32> %scanned, i32 1
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare <2 x i32> @llvm.spv.wave.prefix.sum.v2i32(<2 x i32>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
