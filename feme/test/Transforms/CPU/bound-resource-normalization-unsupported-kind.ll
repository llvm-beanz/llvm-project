; RUN: feme-opt --llvm -passes=feme-cpu-normalize-bound-resources -S %s | FileCheck %s

; A bound handle whose resource kind `feme::cpu::ResourceLoweringPass`
; doesn't canonicalize (a constant buffer, here) is left entirely
; unmodified, matching that pass's own scope note: normalization changes
; addressing, not the set of implemented resource operations (see
; "Bound-resource normalization" in feme/docs/FeMeCPUDesign.md).

target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK: @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 1, i32 0, ptr null)
define void @main() {
  %h = call target("dx.CBuffer", target("dx.Layout", { <4 x float> }, 16, 0))
      @llvm.dx.resource.handlefrombinding.tdx.CBuffer_tdx.Layout_s_sl_v4f32sss_16_0tt(
          i32 0, i32 0, i32 1, i32 0, ptr null)
  ret void
}

; CHECK-NOT: !feme.cpu.bound_resources

declare target("dx.CBuffer", target("dx.Layout", { <4 x float> }, 16, 0))
    @llvm.dx.resource.handlefrombinding.tdx.CBuffer_tdx.Layout_s_sl_v4f32sss_16_0tt(
        i32, i32, i32, i32, ptr)
