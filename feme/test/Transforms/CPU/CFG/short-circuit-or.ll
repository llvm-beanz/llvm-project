; RUN: feme-opt --llvm -passes=feme-cpu-prepare -verify-structured -S %s -o /dev/null

; `if (a || b)`: the mirror image of short-circuit-and.ll -- `b` is only
; evaluated if `a` is false. See "CFG restructurization test suite" in
; feme/docs/FeMeCPUDesign.md.

define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %a = icmp eq i32 %tid, 0
  br i1 %a, label %true, label %check.b
check.b:
  %b = icmp eq i32 %tid, 1
  br i1 %b, label %true, label %false
true:
  br label %end
false:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
