; RUN: opt -S -dxil-op-lower %s | FileCheck %s

target triple = "dxil-pc-shadermodel6.6-compute"

define <2 x i32> @test_getdimensions_xy_no_mips() {
  ; CHECK: %[[HANDLE:.*]] = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217,
  ; CHECK-NEXT: %[[ANNOT_HANDLE:.*]] = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %[[HANDLE]]
  %handle = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)

  ; CHECK-NEXT: %[[RETVAL:.*]] = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %[[ANNOT_HANDLE]], i32 undef)
  ; CHECK-NEXT: %[[WIDTH:.*]] = extractvalue %dx.types.Dimensions %[[RETVAL]], 0
  ; CHECK-NEXT: %[[HEIGHT:.*]] = extractvalue %dx.types.Dimensions %[[RETVAL]], 1
  ; CHECK-NEXT: %[[DIM0:.*]] = insertelement <2 x i32> undef, i32 %[[WIDTH]], i32 0
  ; CHECK-NEXT: %[[DIM1:.*]] = insertelement <2 x i32> %[[DIM0]], i32 %[[HEIGHT]], i32 1
  %1 = call <2 x i32> @llvm.dx.resource.getdimensions.xy(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %handle)

  ; CHECK-NEXT: ret <2 x i32> %[[DIM1]]
  ret <2 x i32> %1
}
