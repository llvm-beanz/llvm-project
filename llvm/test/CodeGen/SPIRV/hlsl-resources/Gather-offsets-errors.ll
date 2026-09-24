; RUN: split-file %s %t
; RUN: not llc -O0 -mtriple=spirv-vulkan-compute %t/non-const-offset.ll -o - 2>&1 | FileCheck %s --check-prefix=NON-CONST
; RUN: not llc -O0 -mtriple=spirv-vulkan-compute %t/cube.ll -o - 2>&1 | FileCheck %s --check-prefix=CUBE
; RUN: not llc -O0 -mtriple=spirv-vulkan-compute %t/mismatched-types.ll -o - 2>&1 | FileCheck %s --check-prefix=MISMATCH

;--- non-const-offset.ll

; NON-CONST: Each of the 4 offsets to a ConstOffsets gather must be a compile-time constant.

@.str = private unnamed_addr constant [4 x i8] c"img\00", align 1
@.str.1 = private unnamed_addr constant [5 x i8] c"samp\00", align 1
@offset_var = private global <2 x i32> zeroinitializer

define void @main() {
entry:
  %img = tail call target("spirv.Image", float, 1, 0, 0, 0, 1, 0) @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_0_0_0_1_0t(i32 0, i32 0, i32 1, i32 0, ptr @.str)
  %sampler = tail call target("spirv.Sampler") @llvm.spv.resource.handlefrombinding.tspirv.Samplert(i32 0, i32 1, i32 1, i32 0, ptr @.str.1)
  %off_dyn = load <2 x i32>, ptr @offset_var
  %res = call <4 x float> @llvm.spv.resource.gather.offsets.v4f32.tspirv.Image_f32_1_0_0_0_1_0t.tspirv.Samplert.v2f32.i32.v2i32.v2i32.v2i32.v2i32(target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img, target("spirv.Sampler") %sampler, <2 x float> zeroinitializer, i32 0, <2 x i32> %off_dyn, <2 x i32> <i32 1, i32 1>, <2 x i32> <i32 1, i32 1>, <2 x i32> <i32 1, i32 1>)
  ret void
}

declare target("spirv.Image", float, 1, 0, 0, 0, 1, 0) @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_0_0_0_1_0t(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler") @llvm.spv.resource.handlefrombinding.tspirv.Samplert(i32, i32, i32, i32, ptr)
declare <4 x float> @llvm.spv.resource.gather.offsets.v4f32.tspirv.Image_f32_1_0_0_0_1_0t.tspirv.Samplert.v2f32.i32.v2i32.v2i32.v2i32.v2i32(target("spirv.Image", float, 1, 0, 0, 0, 1, 0), target("spirv.Sampler"), <2 x float>, i32, <2 x i32>, <2 x i32>, <2 x i32>, <2 x i32>)

;--- cube.ll

; CUBE: Gather operations with offsets are not supported for Cube images.

@.str = private unnamed_addr constant [4 x i8] c"img\00", align 1
@.str.1 = private unnamed_addr constant [5 x i8] c"samp\00", align 1

define void @main() {
entry:
  %img = tail call target("spirv.Image", float, 3, 0, 0, 0, 1, 0) @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_3_0_0_0_1_0t(i32 0, i32 0, i32 1, i32 0, ptr @.str)
  %sampler = tail call target("spirv.Sampler") @llvm.spv.resource.handlefrombinding.tspirv.Samplert(i32 0, i32 1, i32 1, i32 0, ptr @.str.1)
  %res = call <4 x float> @llvm.spv.resource.gather.offsets.v4f32.tspirv.Image_f32_3_0_0_0_1_0t.tspirv.Samplert.v3f32.i32.v2i32.v2i32.v2i32.v2i32(target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img, target("spirv.Sampler") %sampler, <3 x float> zeroinitializer, i32 0, <2 x i32> <i32 1, i32 1>, <2 x i32> <i32 1, i32 1>, <2 x i32> <i32 1, i32 1>, <2 x i32> <i32 1, i32 1>)
  ret void
}

declare target("spirv.Image", float, 3, 0, 0, 0, 1, 0) @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_3_0_0_0_1_0t(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler") @llvm.spv.resource.handlefrombinding.tspirv.Samplert(i32, i32, i32, i32, ptr)
declare <4 x float> @llvm.spv.resource.gather.offsets.v4f32.tspirv.Image_f32_3_0_0_0_1_0t.tspirv.Samplert.v3f32.i32.v2i32.v2i32.v2i32.v2i32(target("spirv.Image", float, 3, 0, 0, 0, 1, 0), target("spirv.Sampler"), <3 x float>, i32, <2 x i32>, <2 x i32>, <2 x i32>, <2 x i32>)

;--- mismatched-types.ll

; MISMATCH: All 4 offsets to a ConstOffsets gather must share the same vector type.

@.str = private unnamed_addr constant [4 x i8] c"img\00", align 1
@.str.1 = private unnamed_addr constant [5 x i8] c"samp\00", align 1

define void @main() {
entry:
  %img = tail call target("spirv.Image", float, 1, 0, 0, 0, 1, 0) @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_0_0_0_1_0t(i32 0, i32 0, i32 1, i32 0, ptr @.str)
  %sampler = tail call target("spirv.Sampler") @llvm.spv.resource.handlefrombinding.tspirv.Samplert(i32 0, i32 1, i32 1, i32 0, ptr @.str.1)
  %res = call <4 x float> @llvm.spv.resource.gather.offsets.v4f32.tspirv.Image_f32_1_0_0_0_1_0t.tspirv.Samplert.v2f32.i32.v2i32.v2i64.v2i32.v2i32(target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img, target("spirv.Sampler") %sampler, <2 x float> zeroinitializer, i32 0, <2 x i32> <i32 1, i32 1>, <2 x i64> <i64 1, i64 1>, <2 x i32> <i32 1, i32 1>, <2 x i32> <i32 1, i32 1>)
  ret void
}

declare target("spirv.Image", float, 1, 0, 0, 0, 1, 0) @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_0_0_0_1_0t(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler") @llvm.spv.resource.handlefrombinding.tspirv.Samplert(i32, i32, i32, i32, ptr)
declare <4 x float> @llvm.spv.resource.gather.offsets.v4f32.tspirv.Image_f32_1_0_0_0_1_0t.tspirv.Samplert.v2f32.i32.v2i32.v2i64.v2i32.v2i32(target("spirv.Image", float, 1, 0, 0, 0, 1, 0), target("spirv.Sampler"), <2 x float>, i32, <2 x i32>, <2 x i64>, <2 x i32>, <2 x i32>)
