; RUN: not feme-opt --llvm -passes=feme-cpu-linearize -S %s 2>&1 | FileCheck %s

; A divergent branch with an empty arm (the "true" successor is the
; reconvergence block itself, i.e. an `if` with no `else`-equivalent body on
; that side) is not yet supported: the general case needs to redirect the
; edge the branch itself owns rather than a distinct arm block's tail, which
; this milestone does not yet generalize to -- see the Status section's
; milestone 6 deviation note in feme/docs/FeMeCPUDesign.md. Diagnosed and
; left untouched.

; CHECK: feme-cpu-linearize: function 'main': an empty diamond arm (in 'entry') is not yet supported
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
