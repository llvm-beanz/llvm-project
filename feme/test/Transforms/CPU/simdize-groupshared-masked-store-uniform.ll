; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap step R23: "a masked store at a uniform address" -- found writing
; `reduction.hlsl` (feme/docs/Roadmap.md's §1.6, R5's own comment) -- closes
; the last of R23's three recorded shapes. A `store` under a divergent
; branch is masked into a `feme.cpu.masked.store` call by
; `feme::cpu::LinearizePass::maskMemoryOps` regardless of whether its own
; address varies by lane; `FunctionWidener::widenMaskedStore` always widens
; that into a real `llvm.masked.scatter`, even when the address is a
; direct, uniform global reference like `@shared`/`@other` here, so
; `feme::cpu::rewriteGroupSharedGlobals` must retarget the resulting
; same-value broadcast `<4 x ptr>` (built once each address folds through
; `ConstantFolder`, then re-materialized into real `insertelement`s by
; `llvm::convertUsersOfConstantsToInstructions`) rather than a direct
; `store`.

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK-NOT: addrspace(3)
; CHECK: if.then:
; CHECK: %shared.flat = getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK: %shared.flat.splat.splat = shufflevector <4 x ptr> {{.*}}%shared.flat{{.*}}, <4 x ptr> poison, <4 x i32> zeroinitializer
; CHECK: call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> splat (i32 42), <4 x ptr> align 4 %shared.flat.splat.splat, <4 x i1> %masked.mask)
; CHECK: if.else:
; CHECK: %other.flat = getelementptr i8, ptr %wave_groupshared, i64 4
; CHECK: %other.flat.splat.splat = shufflevector <4 x ptr> {{.*}}%other.flat{{.*}}, <4 x ptr> poison, <4 x i32> zeroinitializer
; CHECK: call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> splat (i32 7), <4 x ptr> align 4 %other.flat.splat.splat, <4 x i1> %masked.mask2)
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
  %cond = icmp eq i32 %tid, 0
  br i1 %cond, label %if.then, label %if.else

if.then:
  store i32 42, ptr addrspace(3) @shared
  br label %if.end

if.else:
  store i32 7, ptr addrspace(3) @other
  br label %if.end

if.end:
  ret void
}
@shared = internal addrspace(3) global i32 undef
@other = internal addrspace(3) global i32 undef
declare i32 @llvm.dx.thread.id.in.group(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
