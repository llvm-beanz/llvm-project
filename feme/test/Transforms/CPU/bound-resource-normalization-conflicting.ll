; RUN: feme-opt --llvm -passes=feme-cpu-normalize-bound-resources -S %s | FileCheck %s

; Two bindings that disagree about the same (space, register) identity's
; range size are a conflicting declaration (see "Bound-resource
; normalization" in feme/docs/FeMeCPUDesign.md: "conflicting declarations of
; the same binding are diagnosed"). Both are left as `handlefrombinding`
; calls rather than normalized -- `feme::cpu::checkSupportedRaisedOps` still
; rejects them, the same diagnostic as an unbounded range.

target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK: @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 4, i32 %idx, ptr null)
; CHECK: @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 8, i32 %idx2, ptr null)
define void @main(i32 %idx, i32 %idx2) {
  %h1 = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefrombinding.tdx.RawBuffer_i8_1_0t(i32 0, i32 0, i32 4, i32 %idx, ptr null)
  %h2 = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefrombinding.tdx.RawBuffer_i8_1_0t(i32 0, i32 0, i32 8, i32 %idx2, ptr null)
  ret void
}

; CHECK-NOT: !feme.cpu.bound_resources

declare target("dx.RawBuffer", i8, 1, 0)
    @llvm.dx.resource.handlefrombinding.tdx.RawBuffer_i8_1_0t(i32, i32, i32, i32, ptr)
