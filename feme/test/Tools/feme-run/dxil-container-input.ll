; REQUIRES: directx-registered-target
; RUN: split-file %s %t
; RUN: llc %t/shader.ll --filetype=obj -o %t/shader.dxcontainer
; RUN: feme-run --wave-size=4 --groups=1,1,1 \
; RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s

; feme-run's own file comment describes the DXIL bitcode/DXContainer
; import this covers: `loadModule` sniffs the input's format and, for DXIL,
; runs the same import + op/metadata raising `feme::Driver` runs before any
; target-specific lowering, closing the "DXIL/SPIR-V import ... is not yet
; wired into this tool" gap roadmap milestone 4's own deviation note (see
; feme/docs/FeMeCPUDesign.md's Status section) used to describe for DXIL.
;
; The fixture is built by `llc` from this file's textual IR rather than
; checked in as a binary, per "Avoiding binary test fixtures" in
; feme/docs/Design.md -- the same convention feme-dxil-to-dxil.ll et al.
; (feme/test/Tools/feme) already use for a DXContainer fixture. This shader
; is register-bound (`llvm.dx.resource.handlefrombinding`, at `u0`): LLVM's
; DirectX backend has no forward-lowering for
; `llvm.dx.resource.handlefromheap` yet (see the DXIL section's "Raised IR
; prerequisites" deviation note in feme/docs/FeMeCPUDesign.md), so a real,
; `llc`-built DXContainer fixture can only ever carry a register-bound
; handle. `feme::cpu::BoundResourceNormalizationPass` (roadmap milestone 11)
; normalizes it into the CPU target's bindless heap directly -- no bridge
; needed -- so this test covers both the DXIL import/raising path and
; bound-resource normalization together, in one non-HLSL fixture. See
; feme/test/Tools/feme-run/HLSL for the same normalization exercised from
; real Clang-compiled HLSL.

; CHECK: binding[0:0][0]: 0 1 2 3

;--- shader.ll
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

;--- heap.yaml
bindings:
  - space: 0
    register: 0
    entries:
      - index: 0
        size: 16
