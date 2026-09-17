; RUN: feme-opt --llvm -passes=feme-cpu-localize-private-globals,feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H170: a module-scope SPIR-V `Private`-storage global (here
; standing in for a GLSL file-scope `highp float intermediateStore;`,
; imported as a plain, non-addrspace-tagged LLVM `GlobalVariable`) is
; per-invocation storage but is never eligible for SROA/mem2reg's
; alloca-only promotion, and `feme::cpu::SIMDizePass` has no widening
; rule of its own for a bare `GlobalVariable` -- only for a real local
; variable's `alloca`. `feme::cpu::LocalizePrivateGlobalsPass` must
; convert it into one, in its single user function's entry block, so it
; flows through `SIMDizePass`'s existing per-lane widening machinery
; instead of every lane silently sharing one scalar address. Reduced
; from `dEQP-VK.glsl.derivate.dfdx.private_store.*`'s own full-black-
; framebuffer failure.

; CHECK-LABEL: define void @main(
; CHECK-NOT: @intermediateStore
; CHECK: %intermediateStore.perlane = alloca [4 x float]
; Every lane stores its own value into its own address.
; CHECK: store float %{{.*}}, ptr %.lane0
; CHECK: store float %{{.*}}, ptr %.lane1
; CHECK: store float %{{.*}}, ptr %.lane2
; CHECK: store float %{{.*}}, ptr %.lane3
; Every lane reads its own value back, not a single broadcast scalar.
; CHECK: %{{.*}} = load float, ptr %r.lane0
; CHECK: %{{.*}} = load float, ptr %r.lane1
; CHECK: %{{.*}} = load float, ptr %r.lane2
; CHECK: %{{.*}} = load float, ptr %r.lane3
@intermediateStore = private global float undef

define void @main(ptr %out) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  store float %tidf, ptr @intermediateStore
  %r = load float, ptr @intermediateStore
  %addr = getelementptr float, ptr %out, i32 %tid
  store float %r, ptr %addr
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
