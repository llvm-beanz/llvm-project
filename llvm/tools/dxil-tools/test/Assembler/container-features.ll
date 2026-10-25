; RUN: dxil-as --filetype=dxcontainer %s -o %t.dxil
; RUN: obj2yaml %t.dxil | FileCheck %s --check-prefix=WAVE
; RUN: llvm-objdump -s -j SFI0 %t.dxil | FileCheck %s --check-prefix=DOUBLES
; RUN: sed 's/i64 4/i64 32/' %s | dxil-as --filetype=dxcontainer -o %t.min.dxil
; RUN: llvm-objdump -s -j SFI0 %t.min.dxil | FileCheck %s --check-prefix=MIN
; RUN: sed 's/i64 4/i64 8388640/' %s | dxil-as --filetype=dxcontainer -o %t.native.dxil
; RUN: llvm-objdump -s -j SFI0 %t.native.dxil | FileCheck %s --check-prefix=NATIVE
; RUN: sed 's/i64 4/i64 524288/' %s | dxil-as --filetype=dxcontainer -o %t.wave.dxil
; RUN: llvm-objdump -s -j SFI0 %t.wave.dxil | FileCheck %s --check-prefix=WAVEFLAG
; RUN: sed 's/i64 4/i64 8589934592/' %s | dxil-as --filetype=dxcontainer -o %t.alias.dxil
; RUN: llvm-objdump -s -j SFI0 %t.alias.dxil | FileCheck %s --check-prefix=ALIAS

; DOUBLES: 01000000 00000000
; MIN: 10000000 00000000
; NATIVE: 00000400 00000000
; WAVEFLAG: 00400000 00000000
; ALIAS: 00000000 00000000
; WAVE: MinimumWaveLaneCount: 32
; WAVE-NEXT: MaximumWaveLaneCount: 32
; WAVE: EntryName: main
; WAVE: Program:
; WAVE-NEXT: MajorVersion: 6
; WAVE-NEXT: MinorVersion: 6
; WAVE-NEXT: ShaderKind: 5
; WAVE: DXILMajorVersion: 1
; WAVE-NEXT: DXILMinorVersion: 6

define void @main() { ret void }
!dx.version = !{!0}
!dx.valver = !{!1}
!dx.shaderModel = !{!2}
!dx.entryPoints = !{!3}
!0 = !{i32 1, i32 6}
!1 = !{i32 1, i32 8}
!2 = !{!"cs", i32 6, i32 6}
!3 = !{void ()* @main, !"main", null, null, !4}
!4 = !{i32 0, i64 4, i32 4, !5, i32 11, !6}
!5 = !{i32 8, i32 2, i32 1}
!6 = !{i32 32}
