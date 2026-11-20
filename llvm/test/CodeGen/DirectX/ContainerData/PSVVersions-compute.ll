; RUN: split-file %s %t
; RUN: cat %t/shader.ll %t/v10.ll > %t/10.ll
; RUN: cat %t/shader.ll %t/v11.ll > %t/11.ll
; RUN: cat %t/shader.ll %t/v15.ll > %t/15.ll
; RUN: cat %t/shader.ll %t/v16.ll > %t/16.ll
; RUN: cat %t/shader.ll %t/v17.ll > %t/17.ll
; RUN: cat %t/shader.ll %t/v18.ll > %t/18.ll
; RUN: cat %t/shader.ll %t/v00.ll > %t/00.ll
; RUN: llc -filetype=obj %t/10.ll -o - | obj2yaml | FileCheck %s --check-prefix=PSV0
; RUN: llc -filetype=obj %t/11.ll -o - | obj2yaml | FileCheck %s --check-prefix=PSV1
; RUN: llc -filetype=obj %t/15.ll -o - | obj2yaml | FileCheck %s --check-prefix=PSV1
; RUN: llc -filetype=obj %t/16.ll -o - | obj2yaml | FileCheck %s --check-prefix=PSV2
; RUN: llc -filetype=obj %t/17.ll -o - | obj2yaml | FileCheck %s --check-prefix=PSV2
; RUN: llc -filetype=obj %t/18.ll -o - | obj2yaml | FileCheck %s --check-prefix=PSV3
; RUN: llc -filetype=obj %t/00.ll -o - | obj2yaml | FileCheck %s --check-prefix=PSV3
; RUN: llc -filetype=obj %t/shader.ll -o - | obj2yaml | FileCheck %s --check-prefix=PSV3

; Compute shaders have empty I/O signatures, but their PSV ABI still follows
; the validator version. Missing/disabled validator versions retain PSV v3.
; PSV0: Name: PSV0
; PSV0: Version: 0
; PSV0-NOT: NumThreadsX:
; PSV0-NOT: EntryName:
; PSV1: Name: PSV0
; PSV1: Version: 1
; PSV1-NOT: NumThreadsX:
; PSV1-NOT: EntryName:
; PSV2: Name: PSV0
; PSV2: Version: 2
; PSV2: NumThreadsX: 8
; PSV2-NEXT: NumThreadsY: 1
; PSV2-NEXT: NumThreadsZ: 1
; PSV2-NOT: EntryName:
; PSV3: Name: PSV0
; PSV3: Version: 3
; PSV3: NumThreadsX: 8
; PSV3-NEXT: NumThreadsY: 1
; PSV3-NEXT: NumThreadsZ: 1
; PSV3: EntryName: main

;--- shader.ll
target triple = "dxil-pc-shadermodel6.0-compute"
define void @main() #0 {
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,1,1" }

;--- v10.ll
!dx.valver = !{!99}
!99 = !{i32 1, i32 0}
;--- v11.ll
!dx.valver = !{!99}
!99 = !{i32 1, i32 1}
;--- v15.ll
!dx.valver = !{!99}
!99 = !{i32 1, i32 5}
;--- v16.ll
!dx.valver = !{!99}
!99 = !{i32 1, i32 6}
;--- v17.ll
!dx.valver = !{!99}
!99 = !{i32 1, i32 7}
;--- v18.ll
!dx.valver = !{!99}
!99 = !{i32 1, i32 8}
;--- v00.ll
!dx.valver = !{!99}
!99 = !{i32 0, i32 0}
