; RUN: feme-opt --llvm -passes=feme-cpu-prepare -verify-structured -S %s -o /dev/null

; The named-shape corpus (see "CFG restructurization test suite" in
; feme/docs/FeMeCPUDesign.md): a plain if/else, uniform condition, that
; reconverges immediately. `-verify-structured` checking exit success is
; the whole test.

define void @main(i32 %uniform_cond) #0 {
entry:
  %c = icmp sgt i32 %uniform_cond, 0
  br i1 %c, label %t, label %f
t:
  br label %end
f:
  br label %end
end:
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
