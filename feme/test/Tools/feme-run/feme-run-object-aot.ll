; REQUIRES: directx-registered-target
; RUN: split-file %s %t
; RUN: llc %t/shader.ll --filetype=obj -o %t/shader.dxcontainer
; RUN: feme --target=%feme_host_triple %t/shader.dxcontainer -o %t/shader.o
; RUN: feme-run --object --groups=1,1,1 --heap=%t/heap.yaml %t/shader.o | FileCheck %s

; Roadmap step R8's AOT lit recipe (feme/docs/Roadmap.md's §2.4.5): every
; other end-to-end execution test JITs a raised shader with
; `feme::cpu::JITEngine` (see feme-cpu-loop.ll's own comment for the
; pipeline `feme::Driver::run`'s CPU-target retargeting path and
; `JITEngine::create` share). Nothing before this compiled a shader with
; `feme --target=<host-triple>` -- a real object file exporting the
; `feme_cpu_entry_<name>` ABI symbol, the same path `AOTDispatchTest.cpp`'s
; `gtest` coverage exercises -- and then actually *ran* it:
; `feme-cpu-loop.ll`/`feme-cpu-wave-size.ll` only check the compiled
; object's symbols/headers with `llvm-nm`/`llvm-readobj`. This test's
; `feme-run --object` mode loads that same real object file with
; `orc::LLJIT::addObjectFile` and dispatches its entry point directly
; through `feme::cpu::runDispatch` (the same dispatch loop
; `JITEngine::dispatch` uses -- see feme-run's own file comment), so
; `lit`/`FileCheck` can assert on what the *compiled object* computes, not
; only on what the JIT compiles.
;
; This shader stores its own thread id through a traditionally-bound
; buffer (`register(u0)`, one range of size 1) -- the same shape
; thread-id-store.ll's own dynamic-heap version and loop.hlsl's bound
; version separately cover for the JIT path. `feme-run --object` has no
; `ResourceInfo` to read back from a compiled object (no IR/metadata
; survives codegen), so it cannot place a heap YAML `bindings` entry into
; that range's reserved prefix the way the JIT path's
; `feme::cpu::BoundResourceNormalizationPass` requires (`--object` rejects
; `bindings` for exactly this reason, see feme-run's own file comment) --
; but `resource-heap`'s own index 0 lands at the same physical heap slot 0
; `BoundResourceNormalizationPass` deterministically assigns this shader's
; only range (see `BoundResourceNormalizationTest`/`ResourceInfoTest`), so
; describing it there instead works for exactly this common, single-
; binding-at-heap-base-0 shape.

; CHECK: heap[0]: 0 1 2 3

;--- shader.ll
target triple = "dxil-unknown-shadermodel6.6-compute"

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

;--- heap.yaml
resource-heap:
  - index: 0
    size: 16
