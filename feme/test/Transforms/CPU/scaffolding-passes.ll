; RUN: feme-opt --llvm -passes=feme-cpu-prepare -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-simdize -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-lower-wave -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-wrap-entry -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-prepare,feme-cpu-lower-resources,feme-cpu-linearize,feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -S %s | FileCheck %s

; Every FeMe CPU pipeline pass name (see the "Pipeline Overview" and
; "Command line" sections of feme/docs/FeMeCPUDesign.md) is registered with
; feme-opt and runs -- currently as scaffolding no-ops (roadmap milestone 1,
; see each pass's own header comment under feme/include/feme/Transforms/CPU),
; so the whole pipeline's command-line surface is exercisable end to end
; before any individual phase's transform lands.

; CHECK-LABEL: define void @main(
define void @main() #0 {
  ret void
}

attributes #0 = { "hlsl.numthreads"="8,1,1" "hlsl.shader"="compute" }
