; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L498(a): a plain local `alloca` that an *earlier*
; `feme.cpu.masked.store.*` call already proves `feme::cpu::SIMDizePass`
; must widen into real per-lane storage (`%src` below) must still cause
; its *second-order* consumer -- a load/store round trip copying its
; value, element by element, into a completely different, otherwise
; "innocent" `alloca` (`%dst` below) -- to get the same per-lane
; treatment, even though `%dst`'s own store is, by itself, perfectly
; uniform to `UniformityInfo` (its value operand is built purely from
; loads of `%src`, a *different* alloca). Left unrecognized, `%dst` stays
; one scalar address shared by every lane, so the naive widening
; serializes four scalar stores into it -- leaving only the last lane's
; copy resident -- then broadcasts that single surviving copy back out to
; every lane on the read. Reduced from
; `dEQP-VK.pipeline.*.spec_constant.graphics.*.expression.
; array_size_pass_to_function`'s own `Fail (Values did not match)`
; failure, where this exact load/`insertvalue`/`extractvalue`/store idiom
; appears as the inlined-by-value-array-parameter copy of a
; spec-constant-sized local array into a callee's own array parameter.
; `feme::cpu::SIMDizePass::collectMaskedAllocas` must iterate its
; `UI.isDivergentAtDef`-driven scan to a fixed point, propagating
; "divergent via a memory round trip through an already-known
; `MaskedAllocas` member" forward through such a copy, rather than
; running a single linear pass that can never see a derived alloca
; discovered only *after* its own store was already visited.

; CHECK-LABEL: define void @main(
; CHECK: %src.perlane = alloca [4 x [1 x i32]]
; CHECK: %dst.perlane = alloca [4 x [1 x i32]]
; CHECK-NOT: alloca [1 x i32]
; Every lane copies its own element from its own `%src` slot into its own
; `%dst` slot, not a single broadcast scalar round trip.
; CHECK: %dst.lane0 = getelementptr [4 x [1 x i32]], ptr %dst.perlane, i32 0, i32 0
; CHECK: %dst.lane1 = getelementptr [4 x [1 x i32]], ptr %dst.perlane, i32 0, i32 1
; CHECK: %dst.lane2 = getelementptr [4 x [1 x i32]], ptr %dst.perlane, i32 0, i32 2
; CHECK: %dst.lane3 = getelementptr [4 x [1 x i32]], ptr %dst.perlane, i32 0, i32 3
define void @main(ptr %out) #0 {
entry:
  %src = alloca [1 x i32]
  %dst = alloca [1 x i32]
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %srcgep = getelementptr [1 x i32], ptr %src, i32 0, i32 0
  store i32 %tid, ptr %srcgep
  %v = load i32, ptr %srcgep
  %dstgep = getelementptr [1 x i32], ptr %dst, i32 0, i32 0
  store i32 %v, ptr %dstgep
  %r = load i32, ptr %dstgep
  %addr = getelementptr i32, ptr %out, i32 %tid
  store i32 %r, ptr %addr
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
