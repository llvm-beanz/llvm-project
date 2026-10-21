; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; A divergent branch with an empty arm (the "true" successor is the
; reconvergence block itself, i.e. an `if` with no `else`-equivalent body on
; that side) is a correct no-op arm for `DiamondFlattener::flatten()`'s own
; `Cur == End` early return: that arm simply contributes its own unexecuted
; mask pair unchanged via the merge `select`s below, the same as any other
; diamond arm. Previously misdiagnosed as unsupported (roadmap ENV6(a) found
; and fixed this triaging the shared root cause of the 210-case
; dEQP-VK.api.copy_and_blit.* CTS cluster).

; CHECK: define void @main
; CHECK-NOT: feme-cpu-linearize
; CHECK: %live.merge = select i1 %c, i1 %live.t, i1 %live.f
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %end, label %f
f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
