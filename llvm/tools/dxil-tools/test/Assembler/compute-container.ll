; RUN: dxil-as --filetype=dxcontainer %s -o %t.dxil
; RUN: obj2yaml %t.dxil | FileCheck %s --check-prefix=YAML
; RUN: llvm-objdump --section-headers %t.dxil | FileCheck %s --check-prefix=SECTIONS
; RUN: dxil-as --filetype=dxcontainer --disable-output %s
; RUN: dxil-as --filetype=dxcontainer %s -o - > %t.stdout.dxil
; RUN: dxil-dis %t.stdout.dxil -o %t.ll
; RUN: dxil-as %t.ll -o %t.roundtrip.bc
; RUN: dxil-as --filetype=bc %s -o %t.bc
; RUN: cmp %t.bc %t.roundtrip.bc
; RUN: cmp %t.dxil %t.stdout.dxil
; RUN: dxil-as --filetype=dxcontainer < %s > %t.stdin.dxil
; RUN: cmp %t.dxil %t.stdin.dxil
; RUN: cp %s %t.default.ll
; RUN: dxil-as --filetype=dxcontainer %t.default.ll
; RUN: cmp %t.dxil %t.default.dxil
; RUN: dxil-as %t.default.ll
; RUN: cmp %t.bc %t.default.bc
; RUN: dxil-as --filetype=dxcontainer --preserve-bc-uselistorder=false %s -o %t.no-uselists.dxil
; RUN: dxil-dis %t.no-uselists.dxil -o - | dxil-as --preserve-bc-uselistorder=false -o %t.no-uselists.bc
; RUN: dxil-as --preserve-bc-uselistorder=false %s -o %t.expected.bc
; RUN: cmp %t.expected.bc %t.no-uselists.bc

; YAML:      --- !dxcontainer
; YAML:      Hash: [ 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
; YAML-NEXT:         0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ]
; YAML:      PartCount: 6
; YAML-NEXT: PartOffsets: [ 56, 72, 88, 104, 216, 244 ]
; YAML:      Name: SFI0
; YAML-NEXT: Size: 8
; YAML:      Name: ISG1
; YAML-NEXT: Size: 8
; YAML-NEXT: Signature:
; YAML-NEXT: Parameters: []
; YAML:      Name: OSG1
; YAML-NEXT: Size: 8
; YAML-NEXT: Signature:
; YAML-NEXT: Parameters: []
; YAML:      Name: PSV0
; YAML:      Version: 3
; YAML-NEXT: ShaderStage: 5
; YAML-NEXT: MinimumWaveLaneCount: 0
; YAML-NEXT: MaximumWaveLaneCount: 4294967295
; YAML:      NumThreadsX: 8
; YAML-NEXT: NumThreadsY: 2
; YAML-NEXT: NumThreadsZ: 1
; YAML-NEXT: EntryName: main
; YAML-NEXT: ResourceStride: 24
; YAML-NEXT: RuntimeInfoSize: 52
; YAML:      Type: UAVStructured
; YAML-NEXT: Space: 0
; YAML-NEXT: LowerBound: 0
; YAML-NEXT: UpperBound: 0
; YAML-NEXT: Kind: StructuredBuffer
; YAML:      UsedByAtomic64: false
; YAML:      Name: HASH
; YAML-NEXT: Size: 20
; YAML:      IncludesSource: false
; YAML:      Digest: [ 0xC6, 0x27, 0xF7, 0xB5, 0x6D, 0x3D, 0x48, 0x7A,
; YAML-NEXT:           0xE3, 0x9E, 0xE8, 0x46, 0x2F, 0xF9, 0x1B, 0x12 ]
; YAML:      Name: DXIL
; YAML:      MajorVersion: 6
; YAML-NEXT: MinorVersion: 0
; YAML-NEXT: ShaderKind: 5
; YAML:      DXILMajorVersion: 1
; YAML-NEXT: DXILMinorVersion: 0
; YAML:      DXIL: [ 0x42, 0x43, 0xC0, 0xDE,

; SECTIONS:      Name Size
; SECTIONS:      SFI0 00000008
; SECTIONS-NEXT: ISG1 00000008
; SECTIONS-NEXT: OSG1 00000008
; SECTIONS-NEXT: PSV0 00000068
; SECTIONS-NEXT: HASH 00000014
; SECTIONS-NEXT: DXIL

target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%Output = type { i32 }

define void @main() {
  %handle = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)
  %id = call i32 @dx.op.threadId.i32(i32 93, i32 0)
  %product = mul i32 %id, 7
  %value = add i32 %product, 42
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %handle, i32 %id, i32 0, i32 %value, i32 undef, i32 undef, i32 undef, i8 1)
  ret void
}

declare i32 @dx.op.threadId.i32(i32, i32) nounwind readnone
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1) nounwind readonly
declare void @dx.op.bufferStore.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i8) nounwind

!dx.version = !{!0}
!dx.valver = !{!1}
!dx.shaderModel = !{!2}
!dx.resources = !{!3}
!dx.entryPoints = !{!7}

!0 = !{i32 1, i32 0}
!1 = !{i32 1, i32 8}
!2 = !{!"cs", i32 6, i32 0}
!3 = !{null, !4, null, null}
!4 = !{!5}
!5 = !{i32 0, %Output* undef, !"", i32 0, i32 0, i32 1, i32 12, i1 false, i1 false, i1 false, !6}
!6 = !{i32 1, i32 4}
!7 = !{void ()* @main, !"main", null, !3, !8}
!8 = !{i32 0, i64 16, i32 4, !9}
!9 = !{i32 8, i32 2, i32 1}
