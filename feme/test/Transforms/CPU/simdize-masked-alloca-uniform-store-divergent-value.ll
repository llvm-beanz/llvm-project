; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H170: an *unmasked* store into a plain local variable's
; `alloca` -- one that runs under uniform control flow (so
; `feme::cpu::LinearizePass` never wraps it in a `feme.cpu.masked.store.*`
; call at all) but nonetheless writes a genuinely per-lane-divergent
; *value* (here, each lane's own thread ID) -- must NOT keep sharing one
; single scalar address across every lane, exactly like the divergent-
; control-flow case `simdize-masked-alloca-private.ll` already covers.
; Left unrecognized, `%v`'s alloca and every access through it are
; classified uniform (the address never varies), so the naive widening
; would serialize four scalar stores into the same address -- leaving
; only the last lane's value resident -- and then broadcast that single
; surviving value back out to every lane on the read. Reduced from
; `dEQP-VK.glsl.derivate.dfdx.private_store.*`'s own full-black-
; framebuffer failure (there, a SPIR-V `Private`-storage global rather
; than a local, but the same uniform-address/divergent-value shape).
; `feme::cpu::SIMDizePass` must instead give `%v` real per-lane storage,
; both for the unconditional write (`widenMaskedAllocaStore`) and the
; unconditional read-back (`widenMaskedAllocaLoad`).

; CHECK-LABEL: define void @main(
; CHECK: %v.perlane = alloca [4 x float]
; CHECK-NOT: alloca float
; CHECK: %v.lane0 = getelementptr [4 x float], ptr %v.perlane, i32 0, i32 0
; CHECK: %v.lane1 = getelementptr [4 x float], ptr %v.perlane, i32 0, i32 1
; CHECK: %v.lane2 = getelementptr [4 x float], ptr %v.perlane, i32 0, i32 2
; CHECK: %v.lane3 = getelementptr [4 x float], ptr %v.perlane, i32 0, i32 3
; Every lane stores its own value into its own address.
; CHECK: store float %{{.*}}, ptr %.lane0
; CHECK: store float %{{.*}}, ptr %.lane1
; CHECK: store float %{{.*}}, ptr %.lane2
; CHECK: store float %{{.*}}, ptr %.lane3
; Every lane reads its own value back from its own address, not a single
; broadcast scalar load.
; CHECK: %{{.*}} = load float, ptr %r.lane0
; CHECK: %{{.*}} = load float, ptr %r.lane1
; CHECK: %{{.*}} = load float, ptr %r.lane2
; CHECK: %{{.*}} = load float, ptr %r.lane3
define void @main(ptr %out) #0 {
entry:
  %v = alloca float
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  store float %tidf, ptr %v
  %r = load float, ptr %v
  %addr = getelementptr float, ptr %out, i32 %tid
  store float %r, ptr %addr
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
