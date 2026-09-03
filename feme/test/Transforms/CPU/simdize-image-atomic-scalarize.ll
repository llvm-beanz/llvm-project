; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H8v: a real `dEQP-VK.image.atomic_operations.*` case reaches a
; storage-image atomic under *divergent* control flow (each invocation's
; x-coordinate and add operand both come from its own thread id), which
; `feme::cpu::SIMDizePass::widenInstruction`'s dispatch had no entry for --
; `matchImageCall` didn't recognize any of the 11 new
; `feme.cpu.image.atomic.*` kinds yet, so it fell through every other case
; to `widenElementwise`'s hard "unsupported divergent call" error (see
; `spirv-resource-lowering-image-atomic.ll` for the uniform-control-flow
; lowering itself, which this generic-elementwise fallback never reaches).
; `matchImageCall` now recognizes them, and `widenImageCall` -- already
; fully generic over any `feme.cpu.image.*` call -- scalarizes the atomic
; per lane exactly like a divergent sample or store, ANDing the wave's
; side-effect mask into each lane's own call (an atomic's value operand is
; as side-effecting as a store's, so `LaneMaskBase` now treats
; `MatchedImageCall::AtomicValue` the same as `Texel`).

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define void @main(
; CHECK-NOT: call i32 @feme.cpu.image.atomic.add.2d.i32(ptr {{.*}}, i32 {{.*}}, i32 {{.*}}, <4 x i32> {{.*}}
; CHECK-COUNT-4: call i32 @feme.cpu.image.atomic.add.2d.i32(ptr %image_heap, i32 %image_heap_count, i32 0, i32 {{.*}}, i32 0, i32 {{.*}}, i1 {{.*}})
define void @main() #0 {
  %img = call target("spirv.Image", i32, 1, 0, 0, 0, 2, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %tid = call i32 @llvm.spv.flattened.thread.id.in.group()
  %coord = insertelement <2 x i32> poison, i32 %tid, i64 0
  %coord2 = insertelement <2 x i32> %coord, i32 0, i64 1
  %ptr = call ptr @llvm.spv.resource.getpointer.timg(
      target("spirv.Image", i32, 1, 0, 0, 0, 2, 0) %img, <2 x i32> %coord2)
  %old = atomicrmw add ptr %ptr, i32 %tid seq_cst
  ret void
}

; A storage-image atomic every lane performs identically -- same handle,
; same coordinate, same value -- was already correct before this and stays
; a single scalar call: there is nothing to widen.

; CHECK-LABEL: define void @uniform_atomic(
; CHECK: call i32 @feme.cpu.image.atomic.add.2d.i32(
; CHECK-NOT: call i32 @feme.cpu.image.atomic.add.2d.i32(
define void @uniform_atomic() #0 {
  %img = call target("spirv.Image", i32, 1, 0, 0, 0, 2, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 1, i32 1, i32 0, ptr null)
  %ptr = call ptr @llvm.spv.resource.getpointer.timg(
      target("spirv.Image", i32, 1, 0, 0, 0, 2, 0) %img, <2 x i32> zeroinitializer)
  %old = atomicrmw add ptr %ptr, i32 1 seq_cst
  ret void
}

declare target("spirv.Image", i32, 1, 0, 0, 0, 2, 0)
    @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer.timg(
    target("spirv.Image", i32, 1, 0, 0, 0, 2, 0), <2 x i32>)
declare i32 @llvm.spv.flattened.thread.id.in.group()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
