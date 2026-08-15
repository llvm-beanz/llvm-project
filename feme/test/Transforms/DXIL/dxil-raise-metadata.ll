; RUN: feme-opt --llvm -passes=feme-dxil-raise-metadata -S %s | FileCheck %s

; feme::dxil::MetadataRaisingPass (feme/lib/Transforms/DXIL/MetadataRaising.cpp)
; is the inverse of LLVM's `DXILTranslateMetadata` pass: it recovers the
; shader model/pipeline stage a DXIL module was compiled for from its
; `!dx.shaderModel`/`!dx.entryPoints` metadata and re-expresses it the way
; modern LLVM's DirectX backend expects -- as a `shadermodel` target triple
; plus `hlsl.*` function attributes -- so the module can be re-targeted at
; all. See the DXIL section of feme/docs/Design.md.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

; CHECK: target triple = "dxil-unknown-shadermodel6.5-compute"

; CHECK: define void @main() [[ATTRS:#[0-9]+]]
define void @main() {
  ret void
}

; The stage is recorded twice: as the `hlsl.shader` attribute LLVM's DirectX
; backend reads, and as FeMe's own source-independent `feme.shader.stage`
; enumeration ("Stage identity" in feme/docs/FeMeGraphicsDesign.md).
; CHECK-DAG: attributes [[ATTRS]] = {{{.*}}"feme.shader.stage"="compute"{{.*}}"hlsl.numthreads"="1024,1,1"{{.*}}"hlsl.shader"="compute"{{.*}}}

; The `dx.*` named metadata this pass consumes is dropped, since the DirectX
; backend regenerates all of it from scratch when re-emitting DXIL (and it is
; meaningless for any other target). `dx.valver` is deliberately kept -- LLVM's
; own `DXILMetadataAnalysis` reads the original validator version back out of
; it.
; CHECK-NOT: !dx.shaderModel
; CHECK-NOT: !dx.version
; CHECK-NOT: !dx.entryPoints
; CHECK-NOT: !dx.resources
; CHECK: !dx.valver = !{![[#]]}

!dx.version = !{!0}
!dx.valver = !{!1}
!dx.shaderModel = !{!2}
!dx.resources = !{!3}
!dx.entryPoints = !{!4}

!0 = !{i32 1, i32 5}
!1 = !{i32 1, i32 10}
!2 = !{!"cs", i32 6, i32 5}
!3 = !{null, null, null, null}
!4 = !{ptr @main, !"main", null, !3, !5}
!5 = !{i32 4, !6}
!6 = !{i32 1024, i32 1, i32 1}
