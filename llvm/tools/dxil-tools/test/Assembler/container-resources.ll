; RUN: dxil-as --filetype=dxcontainer %s -o %t.dxil
; RUN: obj2yaml %t.dxil | FileCheck %s
; RUN: dxil-dis %t.dxil -o - | dxil-as -o %t.roundtrip.bc
; RUN: dxil-as %s -o %t.bc
; RUN: cmp %t.bc %t.roundtrip.bc

; CHECK: Resources:
; CHECK-NEXT: - Type: CBV
; CHECK-NEXT: Space: 1
; CHECK-NEXT: LowerBound: 4
; CHECK-NEXT: UpperBound: 5
; CHECK-NEXT: Kind: CBuffer
; CHECK: - Type: Sampler
; CHECK-NEXT: Space: 0
; CHECK-NEXT: LowerBound: 0
; CHECK-NEXT: UpperBound: 0
; CHECK-NEXT: Kind: Sampler
; CHECK: - Type: SRVRaw
; CHECK-NEXT: Space: 2
; CHECK-NEXT: LowerBound: 3
; CHECK-NEXT: UpperBound: 4294967295
; CHECK-NEXT: Kind: RawBuffer
; CHECK: - Type: SRVStructured
; CHECK: Kind: StructuredBuffer
; CHECK: - Type: SRVTyped
; CHECK: Kind: Texture2D
; CHECK: - Type: SRVRaw
; CHECK: Kind: RTAccelerationStructure
; CHECK: - Type: UAVStructuredWithCounter
; CHECK-NEXT: Space: 0
; CHECK-NEXT: LowerBound: 0
; CHECK-NEXT: UpperBound: 0
; CHECK-NEXT: Kind: StructuredBuffer
; CHECK-NEXT: Flags:
; CHECK-NEXT: UsedByAtomic64: true
; CHECK-NEXT: SigInputElements: []

define void @main() { ret void }
!dx.version = !{!0}
!dx.valver = !{!1}
!dx.shaderModel = !{!2}
!dx.resources = !{!3}
!dx.entryPoints = !{!7}
!0 = !{i32 1, i32 6}
!1 = !{i32 1, i32 8}
!2 = !{!"cs", i32 6, i32 6}
!3 = !{!10, !4, !11, !12}
!4 = !{!5}
!5 = !{i32 0, i32* undef, !"", i32 0, i32 0, i32 1, i32 12, i1 false, i1 true, i1 false, !6}
!6 = !{i32 1, i32 4, i32 3, i32 1}
!7 = !{void ()* @main, !"main", null, !3, !8}
!8 = !{i32 4, !9}
!9 = !{i32 8, i32 2, i32 1}
!10 = !{!13, !14, !15, !16}
!11 = !{!17}
!12 = !{!18}
!13 = !{i32 0, i32* undef, !"", i32 2, i32 3, i32 -1, i32 11, i32 0, null}
!14 = !{i32 1, i32* undef, !"", i32 0, i32 0, i32 1, i32 12, i32 0, null}
!15 = !{i32 2, i32* undef, !"", i32 0, i32 1, i32 1, i32 2, i32 0, null}
!16 = !{i32 3, i32* undef, !"", i32 0, i32 2, i32 1, i32 16, i32 0, null}
!17 = !{i32 0, i32* undef, !"", i32 1, i32 4, i32 2, i32 16, null}
!18 = !{i32 0, i32* undef, !"", i32 0, i32 0, i32 1, i32 0, null}
