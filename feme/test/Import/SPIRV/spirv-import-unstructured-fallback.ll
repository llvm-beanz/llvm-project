; REQUIRES: spirv-registered-target
; RUN: llc -mtriple=spirv-unknown-vulkan-compute -O0 --filetype=obj %s -o %t.spv
; RUN: feme-translate --import-spirv --import-spirv-structurize-control-flow %t.spv | FileCheck %s

; MLIR's SPIR-V deserializer cannot structurize every legal SPIR-V control
; flow graph: an `OpPhi` in a loop merge block -- which any loop carrying a
; value out of a `break` produces, i.e. most real shader loops -- is
; rejected outright. Its unstructured mode handles the same input fine,
; keeping the original CFG as block arguments and branches, which maps at
; least as directly onto LLVM IR as the structured form does.
;
; feme::SPIRVImporter therefore retries with structurization disabled rather
; than failing (`--import-spirv-structurize-control-flow` above opts back
; into attempting structurization first, purely so this test can exercise
; that retry path directly; see ImportOptions::SPIRVEnableControlFlowStructurization's
; comment for why FeMe's default import path skips straight to unstructured
; deserialization instead of ever attempting -- and retrying away from --
; structurization). This checks that: the fixture below is a loop with a
; conditional early exit, built into a real SPIR-V binary by `llc` rather
; than checked in, per "Avoiding binary test fixtures" in
; feme/docs/Design.md.

target triple = "spirv-unknown-vulkan-compute"

define void @main() #0 {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %latch ]
  %diverged = icmp ugt i32 %i, 10
  br i1 %diverged, label %exit, label %latch

latch:
  %next = add i32 %i, 1
  %continue = icmp ult i32 %next, 100
  br i1 %continue, label %loop, label %exit

exit:
  %result = phi i32 [ %i, %loop ], [ %next, %latch ]
  %used = add i32 %result, 1
  ret void
}

attributes #0 = { "hlsl.numthreads"="1,1,1" "hlsl.shader"="compute" }

; The import succeeds, and produces the unstructured form: plain branches with
; block arguments, and no `spirv.mlir.loop` region.
; CHECK: spirv.module
; CHECK: spirv.func @main()
; CHECK: spirv.Branch
; CHECK-NOT: spirv.mlir.loop
