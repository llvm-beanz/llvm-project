; RUN: feme-opt --llvm -passes=feme-cpu-normalize-bound-resources -S %s | FileCheck %s

; An unbounded traditional range (`handlefrombinding`'s range-size operand
; is 0, DXIL's spelling of an unbounded array -- see
; `raiseResourceHandleFromBinding` in OpRaising.cpp) is left as a
; `handlefrombinding` call rather than normalized: only a finite range can
; be assigned a reserved heap prefix (see "Bound-resource normalization" in
; feme/docs/FeMeCPUDesign.md). `feme::cpu::checkSupportedRaisedOps` still
; rejects it, unchanged from before this pass existed.

target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK: @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 0, i32 %idx, ptr null)
define void @main(i32 %idx) {
  %h = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefrombinding.tdx.RawBuffer_i8_1_0t(i32 0, i32 0, i32 0, i32 %idx, ptr null)
  ret void
}

; CHECK-NOT: !feme.cpu.bound_resources

declare target("dx.RawBuffer", i8, 1, 0)
    @llvm.dx.resource.handlefrombinding.tdx.RawBuffer_i8_1_0t(i32, i32, i32, i32, ptr)
