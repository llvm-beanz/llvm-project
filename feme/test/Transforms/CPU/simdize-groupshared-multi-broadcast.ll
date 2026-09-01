; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L10: the same groupshared global can feed *more than one*
; independent uniform-address broadcast within a single function --
; `llvm::convertUsersOfConstantsToInstructions` memoizes its expansion of a
; broadcast-splat constant per `(Constant, BasicBlock)` pair, so the same
; splat constant reused across multiple basic blocks (one per masked
; gather/scatter site `feme::cpu::FunctionWidener` widens, however many
; there turn out to be) expands into one independent `insertelement` chain
; per block rather than a single one shared function-wide.
; `feme::cpu::(anonymous namespace)::matchPointerBroadcasts` must recognize
; each chain independently instead of assuming exactly one broadcast exists
; function-wide, and `rewriteGroupSharedGlobals`'s `Flat` replacement
; pointer must be built at the function's entry block (dominating every
; block) rather than at either broadcast's own use site.
;
; This reproduces the shape with two divergent branches, each reading
; `@shared` through a masked gather at a *different* program point, so
; `feme::cpu::LinearizePass` + `feme::cpu::FunctionWidener` produce two
; independent broadcasts of `@shared` in two different blocks.

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK-NOT: addrspace(3)
; CHECK: entry:
; CHECK: %shared.flat = getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK: if.then:
; CHECK: %shared.flat.splat.splat{{[0-9]*}} = shufflevector <4 x ptr> {{.*}}%shared.flat{{.*}}, <4 x ptr> poison, <4 x i32> zeroinitializer
; CHECK: call <4 x i32> @llvm.masked.gather.v4i32.v4p0(<4 x ptr> align 4 %shared.flat.splat.splat{{[0-9]*}}, <4 x i1> %masked.mask, <4 x i32> zeroinitializer)
; CHECK: if.else:
; CHECK: %shared.flat.splat.splat{{[0-9]*}} = shufflevector <4 x ptr> {{.*}}%shared.flat{{.*}}, <4 x ptr> poison, <4 x i32> zeroinitializer
; CHECK: call <4 x i32> @llvm.masked.gather.v4i32.v4p0(<4 x ptr> align 4 %shared.flat.splat.splat{{[0-9]*}}, <4 x i1> %masked.mask4, <4 x i32> zeroinitializer)
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
  %cond = icmp eq i32 %tid, 0
  br i1 %cond, label %if.then, label %if.else

if.then:
  %v1 = load i32, ptr addrspace(3) @shared
  store i32 %v1, ptr addrspace(3) @out1
  br label %if.end

if.else:
  %v2 = load i32, ptr addrspace(3) @shared
  store i32 %v2, ptr addrspace(3) @out2
  br label %if.end

if.end:
  ret void
}
@shared = internal addrspace(3) global i32 undef
@out1 = internal addrspace(3) global i32 undef
@out2 = internal addrspace(3) global i32 undef
declare i32 @llvm.dx.thread.id.in.group(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
