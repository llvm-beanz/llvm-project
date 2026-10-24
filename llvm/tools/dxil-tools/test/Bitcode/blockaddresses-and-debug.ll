; RUN: dxil-as %s -o %t.bc
; RUN: dxil-dis %t.bc -o %t.ll
; RUN: FileCheck %s < %t.ll
; RUN: dxil-as %t.ll -o %t.roundtrip.bc
; RUN: cmp %t.bc %t.roundtrip.bc
; RUN: dxil-dis -preserve-dxil-uselistorder %t.bc -o - | dxil-as -o %t.uselists.bc
; RUN: cmp %t.bc %t.uselists.bc
; RUN: llvm-bcanalyzer -dump %t.bc | FileCheck %s --check-prefix=BC

; BC: <DEBUG_LOC op
; BC: <DEBUG_LOC_AGAIN/>
; BC: <DEBUG_LOC op
; BC: <DEBUG_LOC_AGAIN/>
; CHECK: @address = global i8* blockaddress(@target, %exit)
; CHECK: ret i8* blockaddress(@target, %exit)
; CHECK: indirectbr i8* %address, [label %exit]
; CHECK: %a = add i32 0, 0, !dbg ![[FIRST:[0-9]+]]
; CHECK-NEXT: %b = add i32 0, 1, !dbg ![[FIRST]]
; CHECK-NEXT: %c = add i32 0, 2, !dbg ![[SECOND:[0-9]+]]
; CHECK-NEXT: ret void, !dbg ![[SECOND]]
; CHECK: ![[FIRST]] = !DILocation(line: 1,
; CHECK: ![[SECOND]] = !DILocation(line: 2,

@address = global i8* blockaddress(@target, %exit)

define i8* @take_address() {
  ret i8* blockaddress(@target, %exit)
}

define void @target(i8* %address) {
entry:
  indirectbr i8* %address, [label %exit]
exit:
  ret void
}

define void @debug() {
entry:
  %a = add i32 0, 0, !dbg !3
  %b = add i32 0, 1, !dbg !3
  %c = add i32 0, 2, !dbg !4
  ret void, !dbg !4
}

!llvm.module.flags = !{!0}
!0 = !{i32 2, !"Debug Info Version", i32 3}
!1 = !DICompileUnit(language: DW_LANG_C99,
                    file: !DIFile(filename: "shader.hlsl", directory: "/"),
                    subprograms: !{!2})
!2 = !DISubprogram(name: "debug")
!3 = !DILocation(line: 1, scope: !2)
!4 = !DILocation(line: 2, scope: !2)
