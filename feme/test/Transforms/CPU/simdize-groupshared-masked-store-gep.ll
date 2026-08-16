; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap step R23: "an access through a getelementptr", combined with a
; masked store (see simdize-groupshared-masked-store-uniform.ll for the
; direct-global version this generalizes): a groupshared *array* element's
; `store` under a divergent branch is masked into a `feme.cpu.masked.store`
; call the same way a scalar global's is, and its address is a
; `getelementptr` -- an `Instruction`, so `ConstantFolder` cannot fold its
; broadcast away the way it can a direct global reference -- so
; `feme::cpu::rewriteGroupSharedGlobals` must retarget the resulting
; `insertelement`/`shufflevector` broadcast reached through a
; first-level `getelementptr`.

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK-NOT: addrspace(3)
; CHECK: if.then:
; CHECK: %shared.flat = getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK: %ptr{{[0-9]*}} = getelementptr inbounds [4 x i32], ptr %shared.flat, i32 0, i32 2
; CHECK: %ptr{{[0-9]*}}.splat.splat = shufflevector <4 x ptr> {{.*}}%ptr{{[0-9]*}}{{.*}}, <4 x ptr> poison, <4 x i32> zeroinitializer
; CHECK: call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> splat (i32 42), <4 x ptr> align 4 %ptr{{[0-9]*}}.splat.splat, <4 x i1> %masked.mask)
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
  %cond = icmp eq i32 %tid, 0
  br i1 %cond, label %if.then, label %if.else

if.then:
  %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 2
  store i32 42, ptr addrspace(3) %ptr
  br label %if.end

if.else:
  %ptr2 = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 1
  store i32 7, ptr addrspace(3) %ptr2
  br label %if.end

if.end:
  ret void
}
@shared = internal addrspace(3) global [4 x i32] undef
declare i32 @llvm.dx.thread.id.in.group(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
