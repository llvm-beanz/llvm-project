; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L84: a plain (non-groupshared) local variable's `alloca` that
; `feme::cpu::LinearizePass` masks a store into under divergent control
; flow (here, two arms of a divergent `if`/`else`, each storing a
; different constant), then reads back via a masked load in the merge
; block, must NOT keep sharing one single scalar address across every
; lane the way a naive `getWidened`-broadcast fallback would give it --
; each lane's own value would collapse into whichever lane's masked store
; happened to run last (the exact bug this row's own CTS re-run found in
; `Basic/Matrix`'s own `matrix_groupthread_swizzle_{one,zero}_based.test`,
; there reached through a dynamically-indexed local `int4x4`, reduced here
; to its essential, GEP-free scalar shape). `feme::cpu::SIMDizePass` must
; instead give `%v` real per-lane storage (`alloca [4 x i32]`, one real
; address per lane, via `FunctionWidener::widenMaskedAlloca`) so the later
; `llvm.masked.gather` reads back exactly what each lane itself stored.

; CHECK-LABEL: define void @main(
; CHECK: %v.perlane = alloca [4 x i32]
; CHECK-NOT: alloca i32
; CHECK: %v.lane0 = getelementptr [4 x i32], ptr %v.perlane, i32 0, i32 0
; CHECK: %v.lane1 = getelementptr [4 x i32], ptr %v.perlane, i32 0, i32 1
; CHECK: %v.lane2 = getelementptr [4 x i32], ptr %v.perlane, i32 0, i32 2
; CHECK: %v.lane3 = getelementptr [4 x i32], ptr %v.perlane, i32 0, i32 3
; CHECK: t:
; CHECK: call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> splat (i32 10), <4 x ptr> {{.*}}%v.perlane.ptrs{{[0-9]*}}
; CHECK: f:
; CHECK: call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> splat (i32 20), <4 x ptr> {{.*}}%v.perlane.ptrs{{[0-9]*}}
; CHECK: merge:
; CHECK: call <4 x i32> @llvm.masked.gather.v4i32.v4p0(<4 x ptr> {{.*}}%v.perlane.ptrs{{[0-9]*}}
define void @main(ptr %out) #0 {
entry:
  %v = alloca i32
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  store i32 10, ptr %v
  br label %merge
f:
  store i32 20, ptr %v
  br label %merge
merge:
  %r = load i32, ptr %v
  %addr = getelementptr i32, ptr %out, i32 %tid
  store i32 %r, ptr %addr
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
