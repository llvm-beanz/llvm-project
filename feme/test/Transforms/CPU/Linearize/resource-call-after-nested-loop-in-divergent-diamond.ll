; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap H95a: a `feme.cpu.resource.*` call that sits in a loop's exit
; block, where that loop itself is nested inside a still-divergent outer
; `if (tid == 0) { <loop-with-divergent-exit>; <resource call> }` diamond
; (see nested-uniform-loop-in-divergent-diamond.ll for the analogous
; uniform-loop shape this milestone's own L88 fix already covers) -- a real
; reduction of `dEQP-VK.mesh_shader.ext.properties.mesh_shared_memory_size`'s
; own workgroup-shared-memory verification loop, whose trailing scalar
; `result.sharedOK` store failed a pixel comparison ("Unexpected shared
; memory result: 0") because it was always overwritten by every wave in the
; workgroup, not just the one containing the gating invocation.
;
; Root cause: `DiamondFlattener::run` treats every cycle's exit block --
; `exit` here -- as a brand-new root, and (before this fix) always seeded
; it with a hardcoded "every lane is active" mask instead of the real,
; narrower mask (`live.t`/`sideeffect.t`, i.e. "is this the gating
; invocation") that was actually in effect when the loop was reached.
; Because `exit`'s own local shape here doesn't narrow that placeholder
; mask any further either, it constant-folds straight back down to a
; literal `true`, and `applyStageMasks`'s `isKnownConstantMask` check skips
; rewriting the call's mask operand -- silently leaving the resource call
; masked exactly as if every lane, not just the gating one, should execute
; it.

; CHECK-LABEL: define void @main(
; CHECK: exit:
; CHECK: call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i32 1, i1 %sideeffect.t{{[0-9]*}})
define void @main(ptr %heap, i32 %heap_count) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c1 = icmp eq i32 %tid, 0
  br i1 %c1, label %outer.t, label %outer.f
outer.t:
  br label %header
header:
  %i = phi i32 [0, %outer.t], [%inc, %latch]
  %cond = icmp eq i32 %i, %tid
  br i1 %cond, label %exit, label %latch
latch:
  %inc = add i32 %i, 1
  br label %header
exit:
  call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i32 1, i1 true)
  br label %end
outer.f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
