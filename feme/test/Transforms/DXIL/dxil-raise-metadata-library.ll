; RUN: feme-opt --llvm -passes=feme-dxil-raise-metadata -S %s | FileCheck %s

; A `lib` shader model has no single pipeline stage of its own: each entry
; point declares its own stage via the `ShaderKind` (tag 8) entry property.
; This checks that feme::dxil::MetadataRaisingPass gives each entry its own
; `hlsl.shader` attribute accordingly, while the module triple stays
; `-library`, and that a SM 6.6-style single-value `wavesize` (tag 11) is
; widened to the (min, max, preferred) spelling LLVM's `DXILMetadataAnalysis`
; expects.

target triple = "dxil-ms-dx"

; CHECK: target triple = "dxil-unknown-shadermodel6.6-library"

; CHECK: define void @raygen() [[RAYGEN:#[0-9]+]]
define void @raygen() {
  ret void
}

; CHECK: define void @kernel() [[KERNEL:#[0-9]+]]
define void @kernel() {
  ret void
}

; CHECK-DAG: attributes [[RAYGEN]] = {{{.*}}"hlsl.shader"="raygeneration"{{.*}}}
; CHECK-DAG: attributes [[KERNEL]] = {{{.*}}"hlsl.numthreads"="8,4,1"{{.*}}"hlsl.shader"="compute"{{.*}}"hlsl.wavesize"="32,0,0"{{.*}}}

!dx.valver = !{!0}
!dx.shaderModel = !{!1}
!dx.entryPoints = !{!2, !4}

!0 = !{i32 1, i32 10}
!1 = !{!"lib", i32 6, i32 6}
!2 = !{ptr @raygen, !"raygen", null, null, !3}
!3 = !{i32 8, i32 7}
!4 = !{ptr @kernel, !"kernel", null, null, !5}
!5 = !{i32 8, i32 5, i32 4, !6, i32 11, !7}
!6 = !{i32 8, i32 4, i32 1}
!7 = !{i32 32}
