; RUN: dxil-as %s -o %t.bc
; RUN: llvm-bcanalyzer -dump %t.bc | FileCheck %s
; RUN: dxil-as -preserve-bc-uselistorder=false %s -o %t.no-uselist.bc
; RUN: llvm-bcanalyzer -dump %t.no-uselist.bc | FileCheck %s
; RUN: dxil-as -disable-output %s

; Check legacy IR records, not only the generic bitstream signature.
; CHECK: <MODULE_BLOCK
; CHECK-NEXT: <VERSION op0=1/>
; CHECK: <TYPE_BLOCK_ID
; CHECK: <POINTER {{.*}}op0={{[0-9]+}} op1=0/>
; CHECK: <FUNCTION {{.*}}op0=0 op1={{[0-9]+}} op2={{[0-9]+}}
; CHECK: <METADATA_BLOCK
; CHECK: <LOCAL_VAR {{.*}}op1=256
; CHECK: <LOCAL_VAR {{.*}}op1=257
; CHECK: <FUNCTION_BLOCK
; CHECK: <INST_LOAD
; CHECK: <INST_GEP
; CHECK: <INST_PHI

%Pair = type { i32, i32* }

@pair = global %Pair { i32 7, i32* @value }
@value = global i32 42
@half = constant half 0xH3C00
@float = constant float 1.0
@double = constant double 0x3FF0000000000000
@x87 = constant x86_fp80 0xK3FFF8000000000000000
@quad = constant fp128 0xL00000000000000003FFF000000000000
@ppc = constant ppc_fp128 0xM3FF00000000000000000000000000000

define i32 @choose(i1 %cond, i32* %ptr) #0 {
entry:
  %loaded = load i32, i32* %ptr, align 4
  %field = getelementptr %Pair, %Pair* @pair, i32 0, i32 0
  br i1 %cond, label %left, label %right
left:
  %sum = call i32 @add(i32 %loaded, i32 1)
  br label %exit
right:
  br label %exit
exit:
  %result = phi i32 [ %sum, %left ], [ %loaded, %right ]
  ret i32 %result
}

define i32 @add(i32 %a, i32 %b) {
  %sum = add i32 %a, %b
  ret i32 %sum
}

attributes #0 = { nounwind "legacy"="yes" }

!variables = !{!0, !1}
!cycle = !{!2}
!0 = !DILocalVariable(tag: DW_TAG_auto_variable, scope: !3,
                     name: "local", type: !4)
!1 = !DILocalVariable(tag: DW_TAG_arg_variable, scope: !3,
                     name: "arg", type: !4, arg: 1)
!2 = distinct !{!2}
!3 = !DISubprogram(name: "f", isDefinition: false, type: !5)
!4 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!5 = !DISubroutineType(types: !6)
!6 = !{null}
