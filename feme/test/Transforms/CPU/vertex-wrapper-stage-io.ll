; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-vertex -feme-cpu-wave-size=4 -S %s | FileCheck %s

; The vertex wrapper lowers canonical stage IO against `FemeStageLayout` and
; produces the exported `feme_cpu_entry_<name>` symbol.

; CHECK-LABEL: define internal void @vs_main(
; CHECK-NOT: feme.stage.
; CHECK-LABEL: define void @feme_cpu_entry_vs_main(ptr %args) {

define void @vs_main() #0 !feme.signature !0 {
  %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %in, i32 0)
  ret void
}

declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)

attributes #0 = { "feme.shader.stage"="vertex" "feme.cpu.wavesize"="4" }

!0 = !{[152 x i8] c"\04\00\00\00\02\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00"}
