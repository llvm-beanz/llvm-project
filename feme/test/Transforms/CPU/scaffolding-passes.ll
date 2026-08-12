; RUN: feme-opt --llvm -passes=feme-cpu-prepare -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-simdize -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-lower-wave -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-wrap-entry -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-prepare,feme-cpu-lower-resources,feme-cpu-linearize,feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -S %s | FileCheck %s --check-prefix=FULL

; Every FeMe CPU pipeline pass name (see the "Pipeline Overview" and
; "Command line" sections of feme/docs/FeMeCPUDesign.md) is registered with
; feme-opt and runs end to end; roadmap milestone 4 implements Phases 4-6
; for this straight-line, no-op shader (see each pass's own header comment
; under feme/include/feme/Transforms/CPU), which is why the full pipeline's
; expectations (FULL) differ from each phase run in isolation on the
; original, unwidened module -- the point of this test is that the
; command-line surface for every phase name works, not what any individual
; phase does to this trivial input.

; CHECK-LABEL: define void @main(

; FULL-LABEL: define void @feme_cpu_entry_main(
define void @main() #0 {
  ret void
}

attributes #0 = { "hlsl.numthreads"="8,1,1" "hlsl.shader"="compute" }
