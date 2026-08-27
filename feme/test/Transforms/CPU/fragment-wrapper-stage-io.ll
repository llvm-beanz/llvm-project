; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-fragment -feme-cpu-wave-size=4 -S %s | FileCheck %s

; The fragment wrapper lowers canonical stage IO, writes final masks through the
; lowered `feme.cpu.stage.return.masks` helper, and produces the exported entry
; symbol.

; CHECK-LABEL: define internal void @ps_main(
; CHECK-NOT: feme.stage.
; CHECK-NOT: feme.cpu.stage.return.masks
; CHECK-LABEL: define void @feme_cpu_entry_ps_main(ptr %args) {

define void @ps_main() #0 !feme.signature !0 {
  %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %dx = call float @feme.stage.derivative.x.fine.f32(float %in)
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %dx, i32 0)
  ret void
}

declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
declare float @feme.stage.derivative.x.fine.f32(float)
declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)

attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="4" }

!0 = !{[152 x i8] c"\04\00\00\00\02\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00"}
