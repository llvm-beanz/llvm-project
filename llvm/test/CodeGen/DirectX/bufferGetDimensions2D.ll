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

define <3 x i32> @test_getdimensions_xy_with_mips(i32 %mip) {
  ; CHECK-LABEL: define <3 x i32> @test_getdimensions_xy_with_mips(
  %handle = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: %[[D:.*]] = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, {{.*}}, i32 %mip)
  ; CHECK-NEXT: %[[W:.*]] = extractvalue %dx.types.Dimensions %[[D]], 0
  ; CHECK-NEXT: %[[H:.*]] = extractvalue %dx.types.Dimensions %[[D]], 1
  ; CHECK-NEXT: %[[V0:.*]] = insertelement <3 x i32> undef, i32 %[[W]], i32 0
  ; CHECK-NEXT: %[[V1:.*]] = insertelement <3 x i32> %[[V0]], i32 %[[H]], i32 1
  ; CHECK-NEXT: %[[L:.*]] = extractvalue %dx.types.Dimensions %[[D]], 3
  ; CHECK-NEXT: %[[V2:.*]] = insertelement <3 x i32> %[[V1]], i32 %[[L]], i32 2
  %dims = call <3 x i32> @llvm.dx.resource.getdimensions.levels.xy(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %handle, i32 %mip)
  ; CHECK-NEXT: ret <3 x i32> %[[V2]]
  ret <3 x i32> %dims
}

define <2 x i32> @test_getdimensions_x_with_mips(i32 %mip) {
  ; CHECK-LABEL: define <2 x i32> @test_getdimensions_x_with_mips(
  %handle = call target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefrombinding(i32 0, i32 2, i32 1, i32 0, ptr null)
  ; CHECK: %[[D:.*]] = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, {{.*}}, i32 %mip)
  ; CHECK-NEXT: %[[W:.*]] = extractvalue %dx.types.Dimensions %[[D]], 0
  ; CHECK-NEXT: %[[V0:.*]] = insertelement <2 x i32> undef, i32 %[[W]], i32 0
  ; CHECK-NEXT: %[[L:.*]] = extractvalue %dx.types.Dimensions %[[D]], 3
  ; CHECK-NEXT: %[[V1:.*]] = insertelement <2 x i32> %[[V0]], i32 %[[L]], i32 1
  %dims = call <2 x i32> @llvm.dx.resource.getdimensions.levels.x(target("dx.Texture", <4 x float>, 0, 0, 1, 1) %handle, i32 %mip)
  ; CHECK-NEXT: ret <2 x i32> %[[V1]]
  ret <2 x i32> %dims
}
