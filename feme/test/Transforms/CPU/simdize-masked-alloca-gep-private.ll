; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L84: the `getelementptr`-chained analogue of
; simdize-masked-alloca-private.ll -- a `<4 x i32>`-typed local variable
; only accessible through a *dynamic* (non-constant) index, the exact
; shape `Basic/Matrix`'s own `matrix_groupthread_swizzle_{one,zero}_based
; .test` reaches (dxc/LLVM's own mem2reg/SROA cannot promote an alloca
; indexed this way, so it survives to this pass as real memory). Both the
; masked store (under a divergent `if`/`else`) and the later masked load
; go through a `getelementptr` off `%v` with a *uniform* index (`%idx`),
; not `%v` directly -- so this exercises
; `FunctionWidener::widenMaskedAllocaGEP` (the GEP dual of
; `widenMaskedAlloca`), not just the direct-alloca case the other test
; covers.

; CHECK-LABEL: define void @main(
; CHECK: %v.perlane = alloca [4 x <4 x i32>]
; CHECK-NOT: alloca <4 x i32>
; CHECK: %v.lane0 = getelementptr [4 x <4 x i32>], ptr %v.perlane, i32 0, i32 0
; CHECK: %v.lane1 = getelementptr [4 x <4 x i32>], ptr %v.perlane, i32 0, i32 1
; CHECK: %v.lane2 = getelementptr [4 x <4 x i32>], ptr %v.perlane, i32 0, i32 2
; CHECK: %v.lane3 = getelementptr [4 x <4 x i32>], ptr %v.perlane, i32 0, i32 3
; CHECK: t:
; CHECK: %addr.wide = getelementptr <4 x i32>, <4 x ptr> %v.perlane.ptrs{{[0-9]*}}, i32 0, i32 %idx
; CHECK: call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> splat (i32 10), <4 x ptr> {{.*}}%addr.wide
; CHECK: merge:
; CHECK: %raddr.wide = getelementptr <4 x i32>, <4 x ptr> %v.perlane.ptrs{{[0-9]*}}, i32 0, i32 %idx
; CHECK: call <4 x i32> @llvm.masked.gather.v4i32.v4p0(<4 x ptr> {{.*}}%raddr.wide
define void @main(ptr %out) #0 {
entry:
  %v = alloca <4 x i32>
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  ; A uniform index into `%v`, exactly like the real repro's own
  ; loop-carried (but lane-uniform) swizzle index -- deliberately not
  ; `%tid`-derived, to isolate this test from any (correct, unrelated)
  ; widening the index operand itself would otherwise need.
  %idx = call i32 @get_uniform_index()
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %addr = getelementptr <4 x i32>, ptr %v, i32 0, i32 %idx
  store i32 10, ptr %addr
  br label %merge
f:
  br label %merge
merge:
  %raddr = getelementptr <4 x i32>, ptr %v, i32 0, i32 %idx
  %r = load i32, ptr %raddr
  %oaddr = getelementptr i32, ptr %out, i32 %tid
  store i32 %r, ptr %oaddr
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i32 @get_uniform_index()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
