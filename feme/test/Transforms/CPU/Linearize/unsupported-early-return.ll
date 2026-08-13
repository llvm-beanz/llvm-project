; RUN: not feme-opt --llvm -passes=feme-cpu-linearize -S %s 2>&1 | FileCheck %s

; An early `return` inside one arm of a divergent branch has no
; reconvergence point for the other arm to fall through into: "Early `ret`
; under divergence" needs its own mask-update-plus-jump-to-a-unified-exit
; handling (see "Phase 3: Linearization and Predication" in
; feme/docs/FeMeCPUDesign.md), which is not yet implemented -- see the
; Status section's milestone 6 deviation note. This is diagnosed and the
; function is left untouched rather than mistransformed.

; CHECK: feme-cpu-linearize: function 'main': divergent branch in 'entry' has no reconvergence point
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  ret void
f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
