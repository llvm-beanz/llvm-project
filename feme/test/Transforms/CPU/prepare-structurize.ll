; RUN: feme-opt --llvm -passes=feme-cpu-prepare -S %s | FileCheck %s

; Phase 1 (feme::cpu::PreparePass) promotes an `alloca` that mem2reg can
; handle, lowers the `switch`, and structurizes the resulting CFG -- see the
; "Phase 1: Preparation" section of feme/docs/FeMeCPUDesign.md.

; CHECK-LABEL: define void @main(
; CHECK-NOT: alloca
; CHECK-NOT: switch
; CHECK: icmp
define void @main(i32 %v) #0 {
entry:
  %a = alloca i32
  store i32 %v, ptr %a
  %loaded = load i32, ptr %a
  switch i32 %loaded, label %default [ i32 0, label %zero ]
default:
  br label %end
zero:
  br label %end
end:
  ret void
}

attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
