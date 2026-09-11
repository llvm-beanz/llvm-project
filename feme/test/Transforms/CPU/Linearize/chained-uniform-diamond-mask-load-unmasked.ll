; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap H31: two entirely-uniform if/else diamonds in a row (neither
; arm containing a `discard`/`demote`, so neither actually narrows the
; live/side-effect masks), followed by a plain load/store, all inside a
; function that also has a genuinely divergent branch elsewhere (so the
; walk isn't skipped entirely as an all-uniform no-op -- see
; "avoid manufacturing an all-active mask constant" above `flatten`'s own
; caller). `DiamondFlattener::flatten` still creates a real
; `live.merge`/`sideeffect.merge` phi at each uniform diamond's own
; reconvergence point even though both arms carry the identical
; (all-active) mask forward -- see
; `nested-uniform-loop-in-divergent-diamond.ll` for why that phi must not
; simply be skipped. But after the *second* such diamond, the mask
; feeding the final load/store is a phi of a phi, and a naive
; `isa<Constant>(Mask)` check no longer sees through it to the shared
; `true` it's actually always equal to, wrongly treating the trailing
; load/store as needing masking even though every lane is still active.
; The load/store here must stay a plain `load`/`store`, not a
; `feme.cpu.masked.load`/`feme.cpu.masked.store` call.

; CHECK-LABEL: define void @main(
; CHECK-NOT: call {{.*}} @feme.cpu.masked.load
; CHECK-NOT: call void @feme.cpu.masked.store
; CHECK: %loaded = load i32, ptr %p
; CHECK-NEXT: store i32 %loaded, ptr %p
define void @main(ptr %p, i32 %u) #0 {
entry:
  %c1 = icmp eq i32 %u, 0
  br i1 %c1, label %t1, label %f1
t1:
  %a1 = add i32 %u, 1
  br label %end1
f1:
  %b1 = add i32 %u, 2
  br label %end1
end1:
  %v1 = phi i32 [%a1, %t1], [%b1, %f1]
  %c2 = icmp eq i32 %v1, 0
  br i1 %c2, label %t2, label %f2
t2:
  %a2 = add i32 %v1, 10
  br label %end2
f2:
  %b2 = add i32 %v1, 20
  br label %end2
end2:
  %v2 = phi i32 [%a2, %t2], [%b2, %f2]
  %loaded = load i32, ptr %p
  store i32 %loaded, ptr %p
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %cdiv = icmp eq i32 %tid, 0
  br i1 %cdiv, label %div.t, label %div.f
div.t:
  br label %div.end
div.f:
  br label %div.end
div.end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
