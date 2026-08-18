; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Covers roadmap R30's SPIR-V half: a bound 2D sampled image plus a separate
; sampler -- the shape feme::spirv::SampledImagePattern +
; ImageSampleImplicitLodPattern produce (see
; feme/test/Conversion/SPIRVToLLVM/spirv-to-llvm-sampling.mlir) -- lowered
; into the same canonical `feme.cpu.image.*` calls the DXIL side already
; produces. The image and sampler are assigned slot 0 of the *image* and
; *sampler* heaps respectively, which are numbered independently of each
; other and of the resource heap.

target triple = "spirv-unknown-vulkan-compute"

%sampled_image = type { target("spirv.Image", float, 1, 0, 0, 0, 1, 0), target("spirv.Sampler") }

; CHECK-LABEL: define <4 x float> @sample(
; CHECK-SAME: <2 x float> %coord, ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count
define <4 x float> @sample(<2 x float> %coord) {
  %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  %pair0 = insertvalue %sampled_image poison, target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img, 0
  %pair1 = insertvalue %sampled_image %pair0, target("spirv.Sampler") %samp, 1
  %i = extractvalue %sampled_image %pair1, 0
  %s = extractvalue %sampled_image %pair1, 1
  ; CHECK: %[[U:.*]] = extractelement <2 x float> %coord, i64 0
  ; CHECK: %[[V:.*]] = extractelement <2 x float> %coord, i64 1
  ; CHECK: call <4 x float> @feme.cpu.image.sample.2d.v4f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %[[U]], float %[[V]], float 0.000000e+00, i1 false, i1 true)
  %r = call <4 x float> @llvm.spv.resource.sample(
      target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %i,
      target("spirv.Sampler") %s, <2 x float> %coord, <2 x i32> zeroinitializer)
  ret <4 x float> %r
}

; An explicit-LOD sample threads its own LOD operand through and asks the
; runtime helper to honor it, rather than defaulting to level 0.

; CHECK-LABEL: define <4 x float> @sample_level(
define <4 x float> @sample_level(<2 x float> %coord, float %lod) {
  %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call <4 x float> @feme.cpu.image.sample.2d.v4f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %{{.*}}, float %{{.*}}, float %lod, i1 true, i1 true)
  %r = call <4 x float> @llvm.spv.resource.samplelevel(
      target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <2 x float> %coord, float %lod,
      <2 x i32> zeroinitializer)
  ret <4 x float> %r
}

; `OpImageFetch` reaches LLVM IR as a `getpointer` + `load` pair (see
; feme::spirv::ImageLoadPattern), and needs no sampler at all.

; CHECK-LABEL: define <4 x float> @fetch(
define <4 x float> @fetch(<2 x i32> %coord) {
  %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 2, i32 1, i32 0, ptr null)
  %ptr = call ptr @llvm.spv.resource.getpointer.timg(
      target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img, <2 x i32> %coord)
  ; CHECK: %[[X:.*]] = extractelement <2 x i32> %coord, i64 0
  ; CHECK: %[[Y:.*]] = extractelement <2 x i32> %coord, i64 1
  ; CHECK: call <4 x float> @feme.cpu.image.load.2d.v4f32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 %[[X]], i32 %[[Y]], i32 0, i1 true)
  %v = load <4 x float>, ptr %ptr
  ret <4 x float> %v
}

declare target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer.timg(
    target("spirv.Image", float, 1, 0, 0, 0, 1, 0), <2 x i32>)

; The image heap reserves two slots -- binding 0's sampled image and
; binding 2's fetched one -- and the sampler heap one, while the buffer
; resource heap stays empty; each range names the class of the heap its own
; base indexes (0 = buffer, 1 = image, 2 = sampler). `UsesSamplerHeap` (the
; `i1` in the resource node) is per function: `@fetch` needs no sampler.

; CHECK: ![[RMD:[0-9]+]] = !{!"sample", i32 0, i1 true, i32 0, i32 0}
; CHECK: !{!"sample_level", i32 0, i1 true, i32 0, i32 0}
; CHECK: !{!"fetch", i32 0, i1 false, i32 0, i32 0}
; CHECK: ![[BMD:[0-9]+]] = !{!"sample", i32 0, i32 2, i32 1, i32 0, i32 0, i32 1, i32 0, i32 1, i32 0, i32 1, i32 1, i32 0, i32 2, i32 0, i32 2, i32 1, i32 1, i32 1}
