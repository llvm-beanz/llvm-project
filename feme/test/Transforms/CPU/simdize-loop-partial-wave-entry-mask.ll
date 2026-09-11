; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H72: `feme.cpu.mask.any`'s widening (`FunctionWidener::widenMaskAny`,
; `SIMDize.cpp`) previously reduced the widened "is this lane still looping"
; mask directly with `llvm.vector.reduce.or`, without first ANDing it against
; `wave_entry_mask`. A workgroup whose size (here 3) is not a multiple of the
; configured wave width (4) has real padding lanes past its own real thread
; count; a divergent loop's own exit condition (here, an equality against a
; per-lane thread id, mirroring `simdize-loop.ll`'s own uniform-size case) is
; never satisfied for a padding lane, so its "still active" bit would never
; clear and the whole-wave reduction would never go false -- an infinite loop
; at runtime (found via a real `dEQP-VK.mesh_shader.ext.misc.*` case hanging,
; not merely a slow test). `wave_entry_mask` must be ANDed in before the
; reduction so only genuine (non-padding) lanes can keep the loop alive.

; CHECK-LABEL: define void @main(
; CHECK: latch:
; CHECK: %mask.any.real = and <4 x i1> %active.header.live.wide, %wave_entry_mask
; CHECK-NEXT: %loop.any.active = call i1 @llvm.vector.reduce.or.v4i1(<4 x i1> %mask.any.real)
; CHECK-NEXT: %loop.continue = and i1 %loop.cond, %loop.any.active
define void @main(i32 %n) #0 {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %break.cond = icmp eq i32 %tid, %i
  br i1 %break.cond, label %exit, label %latch
latch:
  %inc = add i32 %i, 1
  %loop.cond = icmp slt i32 %inc, %n
  br i1 %loop.cond, label %loop, label %exit
exit:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="3,1,1" }
