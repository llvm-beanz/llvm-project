; REQUIRES: spirv-registered-target
; RUN: llc -mtriple=spirv-unknown-vulkan-compute -O0 --filetype=obj %s -o %t.spv
; RUN: feme-translate --import-spirv --no-implicit-module --spirv-to-llvmir %t.spv | FileCheck %s

; feme::SPIRVImporter's default (ImportOptions::SPIRVEnableControlFlowStructurization
; = false, see that flag's comment) never attempts structured reconstruction at
; all. This matters even for a loop MLIR's structurizer *can* successfully
; rebuild into `spirv.mlir.loop`: that structured op's own `spirv` -> `llvm`
; dialect conversion pattern (`LoopPattern` in
; mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp) asserts
; ("incorrect # of replacement values") on a loop whose merge block carries a
; value -- which any loop with a loop-carried induction variable does, i.e.
; almost every real shader loop, including this trivial counted one with no
; early exit at all. That crash was found by round-tripping real `dxc -spirv`
; compute shaders through this importer while validating roadmap milestone
; V0.5 (see feme/docs/FeMeVulkanDesign.md's "SPIR-V import prerequisites").
;
; This checks that the default import path sidesteps the buggy structured
; pattern entirely: the fixture below -- a real SPIR-V binary built by `llc`
; rather than checked in, per "Avoiding binary test fixtures" in
; feme/docs/Design.md -- imports and converts straight to LLVM IR with no
; flags, producing an ordinary `phi`-based loop rather than crashing.

target triple = "spirv-unknown-vulkan-compute"

define void @main() #0 {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %newsum, %loop ]
  %newsum = add i32 %sum, %i
  %next = add i32 %i, 1
  %cont = icmp ult i32 %next, 8
  br i1 %cont, label %loop, label %exit

exit:
  %result = phi i32 [ %newsum, %loop ]
  %unused = add i32 %result, 0
  ret void
}

attributes #0 = { "hlsl.numthreads"="1,1,1" "hlsl.shader"="compute" }

; CHECK: br label
; CHECK: phi i32
; CHECK: phi i32
; CHECK-NOT: spirv
