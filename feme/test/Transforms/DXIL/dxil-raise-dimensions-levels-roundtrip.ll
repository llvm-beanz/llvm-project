; RUN: opt -S -dxil-op-lower %s | feme-opt --llvm -passes=feme-dxil-raise-ops,verify -S | FileCheck %s
; RUN: opt -S -dxil-op-lower %s | feme-opt --llvm -passes=feme-dxil-raise-ops,feme-dxil-raise-ops,verify -S | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK-LABEL: define <3 x i32> @dynamic(
; CHECK: [[D:%.*]] = call <3 x i32> @llvm.dx.resource.getdimensions.levels.xy{{.*}}({{.*}}, i32 %mip)
; CHECK: extractelement <3 x i32> [[D]], i32 0
; CHECK: extractelement <3 x i32> [[D]], i32 1
; CHECK: extractelement <3 x i32> [[D]], i32 2
; CHECK-NOT: @dx.op.getDimensions
define <3 x i32> @dynamic(i32 %mip) {
  %handle = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %dims = call <3 x i32> @llvm.dx.resource.getdimensions.levels.xy(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %handle, i32 %mip)
  ret <3 x i32> %dims
}

; CHECK-LABEL: define <3 x i32> @array_dynamic(
; CHECK: [[D:%.*]] = call <3 x i32> @llvm.dx.resource.getdimensions.levels.xy{{.*}}(target("dx.Texture", <4 x float>, 0, 0, 1, 6) {{.*}}, i32 %mip)
; CHECK: extractelement <3 x i32> [[D]], i32 0
; CHECK: extractelement <3 x i32> [[D]], i32 1
; CHECK: extractelement <3 x i32> [[D]], i32 2
; CHECK-NOT: @dx.op.getDimensions
define <3 x i32> @array_dynamic(i32 %mip) {
  %handle = call target("dx.Texture", <4 x float>, 0, 0, 1, 6) @llvm.dx.resource.handlefrombinding(i32 0, i32 2, i32 1, i32 0, ptr null)
  %dims = call <3 x i32> @llvm.dx.resource.getdimensions.levels.xy(target("dx.Texture", <4 x float>, 0, 0, 1, 6) %handle, i32 %mip)
  ret <3 x i32> %dims
}

; CHECK-LABEL: define <3 x i32> @zero(
; CHECK: [[D:%.*]] = call <3 x i32> @llvm.dx.resource.getdimensions.levels.xy{{.*}}({{.*}}, i32 0)
; CHECK: extractelement <3 x i32> [[D]], i32 2
; CHECK-NOT: @dx.op.getDimensions
define <3 x i32> @zero() {
  %handle = call target("dx.Texture", i32, 0, 0, 1, 2) @llvm.dx.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
  %dims = call <3 x i32> @llvm.dx.resource.getdimensions.levels.xy(target("dx.Texture", i32, 0, 0, 1, 2) %handle, i32 0)
  ret <3 x i32> %dims
}

; CHECK-LABEL: define <2 x i32> @plain_1d(
; CHECK: [[D:%.*]] = call <2 x i32> @llvm.dx.resource.getdimensions.levels.x{{.*}}({{.*}}, i32 %mip)
; CHECK: extractelement <2 x i32> [[D]], i32 0
; CHECK: extractelement <2 x i32> [[D]], i32 1
; CHECK-NOT: @dx.op.getDimensions
define <2 x i32> @plain_1d(i32 %mip) {
  %handle = call target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefrombinding(i32 0, i32 3, i32 1, i32 0, ptr null)
  %dims = call <2 x i32> @llvm.dx.resource.getdimensions.levels.x(target("dx.Texture", <4 x float>, 0, 0, 1, 1) %handle, i32 %mip)
  ret <2 x i32> %dims
}

; CHECK-LABEL: define i32 @plain_1d_count_only(
; CHECK: [[D:%.*]] = call <2 x i32> @llvm.dx.resource.getdimensions.levels.x{{.*}}({{.*}}, i32 0)
; CHECK: extractelement <2 x i32> [[D]], i32 1
; CHECK-NOT: @dx.op.getDimensions
define i32 @plain_1d_count_only() {
  %handle = call target("dx.Texture", i32, 0, 0, 1, 1) @llvm.dx.resource.handlefrombinding(i32 0, i32 4, i32 1, i32 0, ptr null)
  %dims = call <2 x i32> @llvm.dx.resource.getdimensions.levels.x(target("dx.Texture", i32, 0, 0, 1, 1) %handle, i32 undef)
  %count = extractelement <2 x i32> %dims, i32 1
  ret i32 %count
}
