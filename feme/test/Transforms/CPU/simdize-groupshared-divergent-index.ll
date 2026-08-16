; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap step R23: a genuinely divergent (per-lane-varying) groupshared
; index -- the common `groupshared[threadIdInGroup]` pattern -- closes the
; "divergent index" shape feme/docs/Roadmap.md's §1.6 recorded.
; `FunctionWidener::widenGroupSharedGEP` widens the `getelementptr` itself
; into a real vector-of-pointers access (LLVM allows a scalar base with
; vector index operands, broadcasting the base implicitly) rather than
; `widenScalarizedFallback`'s per-lane clone-and-reassemble via
; `insertelement`, which `feme::cpu::rewriteGroupSharedGlobals` could not
; see through; the (also divergent, since its address is) `load` that
; reads through it becomes a real `llvm.masked.gather`
; (`FunctionWidener::widenGroupSharedLoad`), masked only by the wave's own
; entry mask since nothing else governs an unconditional access like this
; one.

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK-NOT: addrspace(3)
; CHECK: %shared.flat = getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK-NEXT: %ptr.wide{{[0-9]*}} = getelementptr inbounds [4 x i32], ptr %shared.flat, i32 0, <4 x i32> %tid{{[0-9]*}}
; CHECK-NEXT: call <4 x i32> @llvm.masked.gather.v4i32.v4p0(<4 x ptr> align 4 %ptr.wide{{[0-9]*}}, <4 x i1> %wave_entry_mask, <4 x i32> zeroinitializer)
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
  %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 %tid
  %val = load i32, ptr addrspace(3) %ptr
  ret void
}
@shared = internal addrspace(3) global [4 x i32] undef
declare i32 @llvm.dx.thread.id.in.group(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
