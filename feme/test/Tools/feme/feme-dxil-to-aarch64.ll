; REQUIRES: directx-registered-target, aarch64-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer
; RUN: feme --target=aarch64-unknown-linux-gnu %t.dxcontainer -o %t.o
; RUN: llvm-readobj --file-header %t.o | FileCheck %s

; Retargets a DXIL module to a real AArch64 ISA object file through the full
; `feme` CLI (Design.md milestone 9's AArch64 remainder, roadmap step R13's
; §1.5 item): import (feme::DXILImporter) -> raise dx.op.* calls back to
; idiomatic llvm.dx.* intrinsics (feme::dxil::OpRaisingPass) ->
; feme::cpu::runPipeline's SPMD-to-scalar/vector lowering (the same pipeline
; every other CPU-target retarget uses, see feme/docs/FeMeCPUDesign.md) ->
; feme::TargetMachineBackend targeting "aarch64-unknown-linux-gnu". Unlike
; AMDGPU/NVPTX (their own, GPU-shaped `feme::Driver` branches, see
; Driver.cpp's `isCPUTarget`), AArch64 needs no target-specific lowering
; pass at all: `feme::Driver`'s triple resolution and
; `feme::cpu::runPipeline` are already triple-generic (see Design.md
; milestone 9's status note), so this exercises that genericness against a
; real, non-host CPU ISA rather than only the host's own default target --
; the one other CPU-target test/Tools/feme tests (e.g.
; test/Tools/feme-run/feme-run-object-aot.ll) implicitly retarget to via
; `%feme_host_triple`.
;
; `llvm-readobj`'s `Machine` field confirms a real AArch64 ELF object was
; produced, not merely that codegen didn't crash.

target triple = "dxil-unknown-shadermodel6.5-compute"

define void @main() #0 {
  %h = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %offset = mul i32 %tid, 4
  call void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0) %h, i32 %offset, i32 poison, i32 %tid)
  ret void
}
declare target("dx.RawBuffer", i8, 1, 0)
    @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
declare void @llvm.dx.resource.store.rawbuffer.i32(
    target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }

; CHECK: Machine: EM_AARCH64
