; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources -S %s | FileCheck %s --check-prefix=LOWER
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s --check-prefix=SIMD

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

; LOWER-LABEL: define i32 @readonly(
; LOWER: call i32 @llvm.dx.resource.updatecounter
; LOWER-LABEL: define void @main(
; LOWER: call i32 @feme.cpu.resource.atomic.updatecounter.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i32 1, i1 true)
; SIMD-LABEL: define void @main(
; SIMD-COUNT-4: call i32 @feme.cpu.resource.atomic.updatecounter.i32(
; SIMD-NOT: call i32 @feme.cpu.resource.atomic.updatecounter.i32(
define void @main() #0 {
  %h = call target("dx.RawBuffer", i32, 1, 0) @llvm.dx.resource.handlefromheap(i32 0)
  %old = call i32 @llvm.dx.resource.updatecounter(target("dx.RawBuffer", i32, 1, 0) %h, i8 1)
  ret void
}

define i32 @readonly() {
  %h = call target("dx.RawBuffer", i32, 0, 0) @llvm.dx.resource.handlefromheap.readonly(i32 0)
  %old = call i32 @llvm.dx.resource.updatecounter.readonly(target("dx.RawBuffer", i32, 0, 0) %h, i8 1)
  ret i32 %old
}

declare target("dx.RawBuffer", i32, 1, 0) @llvm.dx.resource.handlefromheap(i32)
declare i32 @llvm.dx.resource.updatecounter(target("dx.RawBuffer", i32, 1, 0), i8 immarg)
declare target("dx.RawBuffer", i32, 0, 0) @llvm.dx.resource.handlefromheap.readonly(i32)
declare i32 @llvm.dx.resource.updatecounter.readonly(target("dx.RawBuffer", i32, 0, 0), i8 immarg)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
