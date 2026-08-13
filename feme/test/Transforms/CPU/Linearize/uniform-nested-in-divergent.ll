; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; A uniform if/else nested inside a divergent branch's arm: the uniform
; branch is left as real control flow (recursed into on its own, same as at
; the top level -- see `uniform-diamond-unchanged.ll`), and the divergent
; outer branch's arm tail (`t.end`, the uniform diamond's own reconvergence
; block) is what gets redirected into the outer false arm instead of the
; outer reconvergence block. See "Phase 3: Linearization and Predication" in
; feme/docs/FeMeCPUDesign.md.

; CHECK-LABEL: define void @main(
; CHECK: br label %t
; CHECK: t:
; CHECK: br i1 %uc, label %t.a, label %t.b
; CHECK: t.a:
; CHECK: t.b:
; CHECK: t.end:
; CHECK-NEXT: br label %f
; CHECK: f:
; CHECK-NEXT: br label %end
define void @main(i32 %uniform_cond) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %uc = icmp sgt i32 %uniform_cond, 0
  br i1 %uc, label %t.a, label %t.b
t.a:
  br label %t.end
t.b:
  br label %t.end
t.end:
  br label %end
f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
