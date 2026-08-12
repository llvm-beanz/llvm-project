; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A `feme.cpu.resource.*` call whose operands are all uniform (a
; compile-time-constant descriptor index and offset) stays a single scalar
; call -- "Uniform-address load/store" in "Phase 4: Widening": there is
; nothing to widen.

; CHECK-LABEL: define void @main(
; CHECK: call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
; CHECK-NOT: call i32 @feme.cpu.resource.load.raw.i32(
define void @main() #0 {
  %h = call target("dx.RawBuffer", i32, 1, 0) @llvm.dx.resource.handlefromheap(i32 0, i1 false)
  %loaded = call {i32, i1} @llvm.dx.resource.load.rawbuffer.i32(
      target("dx.RawBuffer", i32, 1, 0) %h, i32 0, i32 0)
  %val = extractvalue {i32, i1} %loaded, 0
  ret void
}
declare target("dx.RawBuffer", i32, 1, 0) @llvm.dx.resource.handlefromheap(i32, i1)
declare {i32, i1} @llvm.dx.resource.load.rawbuffer.i32(target("dx.RawBuffer", i32, 1, 0), i32, i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
