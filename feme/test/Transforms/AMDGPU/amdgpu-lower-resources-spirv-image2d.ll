; RUN: feme-opt --llvm -passes=feme-amdgpu-lower-resources -S %s | FileCheck %s

; feme::amdgpu::ResourceLoweringPass's SPIR-V-flavored accesses (see
; amdgpu-lower-resources-spirv.ll) are not always a flat, scalar-coordinate
; `Buffer`: a `spirv.Image` binding's `Dim` type parameter can instead be a
; genuine 2D/3D image (`Texture2D`/`RWTexture2D`/`Texture3D`/`RWTexture3D`),
; whose multi-component coordinate this pass must linearize the same way it
; already does for a `dx.Texture` binding of more than one dimension (see
; "Raised LLVM IR -> AMDGPU" in feme/docs/Design.md) -- one extra `i32`
; addressing-stride kernel argument per coordinate component beyond the
; first. This also exercises `llvm.spv.resource.load.level`, the intrinsic
; a `Texture2D<T>::Load` reaches this pass as (see `ImageFetchLodPattern` in
; SPIRVToLLVMPatterns.cpp -- `dxc` always gives `Texture2D<T>::Load` an
; explicit, if always-zero, mip level), which -- unlike `getpointer` -- is
; itself the loaded value, not a pointer some other load reads through.
;
; Rewriting an entry point grows its signature, so the rewritten function is
; a new one appended to the module (see amdgpu-lower-resources-spirv.ll's own
; comment); the CHECK order below follows that, not the source order.

target triple = "amdgcn-amd-amdhsa"

; A `Dim` this pass does not model -- `Cube` (3) here -- leaves the entry
; point untouched entirely rather than partially rewritten, the same way an
; unmodeled `dx.Texture` dimension does (see `getTextureCoordComponents`'s
; comment in ResourceLowering.cpp).
; CHECK: define void @cube(<3 x i32> %coord) [[ATTRS:#[0-9]+]]
; CHECK: call target("spirv.Image", float, 3, 0, 0, 0, 1, 0) @llvm.spv.resource.handlefrombinding
define void @cube(<3 x i32> %coord) #0 {
  %h = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_3_0_0_0_1_0t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %value = call <4 x float> @llvm.spv.resource.load.level.v4f32.tspirv.Image_f32_3_0_0_0_1_0t.v3i32.i32.v3i32(
      target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %h, <3 x i32> %coord, i32 0, <3 x i32> zeroinitializer)
  ret void
}

; CHECK: define void @image2d(i32 %x, i32 %y, ptr addrspace(1) %res.space0.reg0, i32 %res.space0.reg0.stride0, ptr addrspace(1) %res.space0.reg1, i32 %res.space0.reg1.stride0)
; CHECK-NEXT: [[COORD0:%.*]] = insertelement <2 x i32> poison, i32 %x, i32 0
; CHECK-NEXT: [[COORD:%.*]] = insertelement <2 x i32> [[COORD0]], i32 %y, i32 1
; CHECK-NEXT: [[X0:%.*]] = extractelement <2 x i32> [[COORD]], i32 0
; CHECK-NEXT: [[Y0:%.*]] = extractelement <2 x i32> [[COORD]], i32 1
; CHECK-NEXT: [[ROW0:%.*]] = mul i32 [[Y0]], %res.space0.reg0.stride0
; CHECK-NEXT: [[IDX0:%.*]] = add i32 [[X0]], [[ROW0]]
; CHECK-NEXT: [[SRC:%.*]] = getelementptr <4 x float>, ptr addrspace(1) %res.space0.reg0, i32 [[IDX0]]
; CHECK-NEXT: [[VALUE:%.*]] = load <4 x float>, ptr addrspace(1) [[SRC]], align 16
; CHECK-NEXT: [[X1:%.*]] = extractelement <2 x i32> [[COORD]], i32 0
; CHECK-NEXT: [[Y1:%.*]] = extractelement <2 x i32> [[COORD]], i32 1
; CHECK-NEXT: [[ROW1:%.*]] = mul i32 [[Y1]], %res.space0.reg1.stride0
; CHECK-NEXT: [[IDX1:%.*]] = add i32 [[X1]], [[ROW1]]
; CHECK-NEXT: [[DST:%.*]] = getelementptr <4 x float>, ptr addrspace(1) %res.space0.reg1, i32 [[IDX1]]
; CHECK-NEXT: store <4 x float> [[VALUE]], ptr addrspace(1) [[DST]], align 16
; CHECK-NEXT: ret void
define void @image2d(i32 %x, i32 %y) #0 {
  %coord0 = insertelement <2 x i32> poison, i32 %x, i32 0
  %coord = insertelement <2 x i32> %coord0, i32 %y, i32 1

  %in = call target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_2_0_0_1_0t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %value = call <4 x float> @llvm.spv.resource.load.level.v4f32.tspirv.Image_f32_1_2_0_0_1_0t.v2i32.i32.v2i32(
      target("spirv.Image", float, 1, 2, 0, 0, 1, 0) %in, <2 x i32> %coord, i32 0, <2 x i32> zeroinitializer)

  %out = call target("spirv.Image", float, 1, 2, 0, 0, 2, 1)
      @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_2_0_0_2_1t(i32 0, i32 1, i32 1, i32 0, ptr null)
  %ptr = call ptr @llvm.spv.resource.getpointer.p0.tspirv.Image_f32_1_2_0_0_2_1t.v2i32(
      target("spirv.Image", float, 1, 2, 0, 0, 2, 1) %out, <2 x i32> %coord)
  store <4 x float> %value, ptr %ptr
  ret void
}

attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,8,1" }

declare target("spirv.Image", float, 3, 0, 0, 0, 1, 0) @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_3_0_0_0_1_0t(i32, i32, i32, i32, ptr)
declare <4 x float> @llvm.spv.resource.load.level.v4f32.tspirv.Image_f32_3_0_0_0_1_0t.v3i32.i32.v3i32(target("spirv.Image", float, 3, 0, 0, 0, 1, 0), <3 x i32>, i32, <3 x i32>)
declare target("spirv.Image", float, 1, 2, 0, 0, 1, 0) @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_2_0_0_1_0t(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 1, 2, 0, 0, 2, 1) @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_2_0_0_2_1t(i32, i32, i32, i32, ptr)
declare <4 x float> @llvm.spv.resource.load.level.v4f32.tspirv.Image_f32_1_2_0_0_1_0t.v2i32.i32.v2i32(target("spirv.Image", float, 1, 2, 0, 0, 1, 0), <2 x i32>, i32, <2 x i32>)
declare ptr @llvm.spv.resource.getpointer.p0.tspirv.Image_f32_1_2_0_0_2_1t.v2i32(target("spirv.Image", float, 1, 2, 0, 0, 2, 1), <2 x i32>)
