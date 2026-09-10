; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources -S %s | FileCheck %s

; Covers feme::cpu::ResourceLoweringPass's stripping of
; `llvm.dx.resource.nonuniformindex`, the marker upstream uses to spell a
; non-uniform descriptor-heap index (see "Lowering" in
; feme/docs/FeMeCPUDesign.md). It is a GPU codegen hint only: the CPU target
; evaluates every lane's index independently regardless, so the marker is
; replaced by its own operand and the wrapped index is lowered exactly as an
; unwrapped one is.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK-NOT: llvm.dx.resource.nonuniformindex

; CHECK-LABEL: define void @nonuniform_heap_index(
; CHECK-SAME: i32 %idx,
; CHECK: call i32 @feme.cpu.resource.load.raw.i32(
; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 %idx,
define void @nonuniform_heap_index(i32 %idx, i32 %byte_offset) {
  %nu = call i32 @llvm.dx.resource.nonuniformindex(i32 %idx)
  %h = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 %nu)
  %loaded = call {i32, i1}
      @llvm.dx.resource.load.rawbuffer.i32.tdx.RawBuffer_i8_1_0t(
          target("dx.RawBuffer", i8, 1, 0) %h, i32 %byte_offset, i32 poison)
  %val = extractvalue {i32, i1} %loaded, 0
  ret void
}

declare i32 @llvm.dx.resource.nonuniformindex(i32)
declare target("dx.RawBuffer", i8, 1, 0)
    @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32)
declare {i32, i1}
    @llvm.dx.resource.load.rawbuffer.i32.tdx.RawBuffer_i8_1_0t(
        target("dx.RawBuffer", i8, 1, 0), i32, i32)
