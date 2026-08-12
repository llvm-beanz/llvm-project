; RUN: feme-opt --llvm -passes=feme-cpu-prepare -verify-structured -S %s -o /dev/null

; A diamond nested inside another diamond's true arm, both divergent (the
; linearizer's harder case: reconvergence points nested inside one
; another). See "CFG restructurization test suite" in
; feme/docs/FeMeCPUDesign.md.

define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c1 = icmp eq i32 %tid, 0
  br i1 %c1, label %outer.t, label %outer.f
outer.t:
  %c2 = icmp eq i32 %tid, 1
  br i1 %c2, label %inner.t, label %inner.f
inner.t:
  br label %outer.end
inner.f:
  br label %outer.end
outer.end:
  br label %end
outer.f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
