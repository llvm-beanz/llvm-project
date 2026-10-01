; RUN: split-file %s %t
; RUN: feme-run --reference --groups=1,1,1 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s

; Roadmap R34, "heap YAML sampler resource class" (see feme/docs/Roadmap.md's
; §2.6.1): the shader itself does not sample anything -- this test's own
; contract is only that a `samplers` heap YAML entry parses and is placed
; into `feme::cpu::DispatchResources::SamplerHeap` at its declared `index`,
; observable through `printHeapContents`'s new `sampler[<index>]:` line (see
; `FemeSamplerDescriptor`'s header comment in RuntimeABI.h for the field
; order each printed word corresponds to). A real end-to-end sampling repro
; is tracked separately once a `llvm.dx.resource.handlefromheap` sampler
; handle and a `load`/`sample` intrinsic pairing exist for this tool to
; exercise.

; All-defaults sampler: Nearest/Nearest/Nearest filtering (0,0,0),
; Repeat/Repeat/Repeat addressing (0,0,0), LodBias=0.0, MinLod=0.0,
; MaxLod=1000.0 (1148846080), CompareFunc=Never (0, unused since
; compare-enable defaults to false), BorderColor=(0,0,0,0),
; MaxAnisotropy=1.0 (1065353216, unused since anisotropy-enable defaults to
; false), ReductionMode=WeightedAverage (0), Flags=0, Reserved={0,0,0}.
; CHECK: sampler[0]: 0 0 0 0 0 0 0 0 1148846080 0 0 0 0 0 1065353216 0 0 0 0 0

;--- shader.ll
define void @main() #0 {
  %out = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 1)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %base = mul i32 %tid, 4
  call void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0) %out, i32 %base, i32 poison, i32 %tid)
  ret void
}
declare target("dx.RawBuffer", i8, 1, 0)
    @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32)
declare void @llvm.dx.resource.store.rawbuffer.i32(
    target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }

;--- heap.yaml
resource-heap:
  - index: 1
    size: 16
samplers:
  - index: 0
