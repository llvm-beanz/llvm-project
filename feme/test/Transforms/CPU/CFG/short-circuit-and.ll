; RUN: feme-opt --llvm -passes=feme-cpu-prepare -verify-structured -S %s -o /dev/null

; `if (a && b)`: evaluating `b` is itself conditional on `a`, so the
; "true" path has an extra block the false path skips -- a diamond whose
; two arms aren't symmetric. See "CFG restructurization test suite" in
; feme/docs/FeMeCPUDesign.md.

define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %a = icmp eq i32 %tid, 0
  br i1 %a, label %check.b, label %false
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
