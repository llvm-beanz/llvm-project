; RUN: feme-opt --llvm -passes=feme-cpu-fold-spirv-builtins -S %s | FileCheck %s

; Covers feme::cpu::SPIRVBuiltinFoldingPass: folds a constant-index
; `extractelement` of an `insertelement` chain back into the single scalar
; value that lane's `insertelement` carries -- the shape
; `feme::spirv::createConvertSPIRVToLLVMPass` always produces for a builtin
; (thread/group ID) input variable access (see this pass's own header
; comment). Only the lane actually extracted (here, %b) needs to survive;
; the chain building every other lane becomes dead and is left for ordinary
; DCE elsewhere in the pipeline to remove.

; CHECK-LABEL: define float @extract_middle_lane(
; CHECK-NOT: extractelement
; CHECK: uitofp i32 %b to float
define float @extract_middle_lane(i32 %a, i32 %b, i32 %c) {
  %1 = insertelement <3 x i32> poison, i32 %a, i32 0
  %2 = insertelement <3 x i32> %1, i32 %b, i32 1
  %3 = insertelement <3 x i32> %2, i32 %c, i32 2
  %4 = extractelement <3 x i32> %3, i32 1
  %5 = uitofp i32 %4 to float
  ret float %5
}

; A non-constant extraction index has no single lane to fold into; left
; untouched.
; CHECK-LABEL: define i32 @dynamic_index(
; CHECK: extractelement
define i32 @dynamic_index(i32 %a, i32 %b, i32 %idx) {
  %1 = insertelement <2 x i32> poison, i32 %a, i32 0
  %2 = insertelement <2 x i32> %1, i32 %b, i32 1
  %3 = extractelement <2 x i32> %2, i32 %idx
  ret i32 %3
}
