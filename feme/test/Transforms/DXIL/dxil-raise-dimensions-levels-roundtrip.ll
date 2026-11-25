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

; CHECK-LABEL: define <3 x i32> @cube_dynamic(
; CHECK: [[D:%.*]] = call <3 x i32> @llvm.dx.resource.getdimensions.levels.xy{{.*}}(target("dx.Texture", <4 x float>, 0, 0, 1, 5) {{.*}}, i32 %mip)
; CHECK: extractelement <3 x i32> [[D]], i32 0
; CHECK: extractelement <3 x i32> [[D]], i32 1
; CHECK: extractelement <3 x i32> [[D]], i32 2
; CHECK-NOT: @dx.op.getDimensions
define <3 x i32> @cube_dynamic(i32 %mip) {
  %handle = call target("dx.Texture", <4 x float>, 0, 0, 1, 5) @llvm.dx.resource.handlefrombinding(i32 0, i32 5, i32 1, i32 0, ptr null)
  %dims = call <3 x i32> @llvm.dx.resource.getdimensions.levels.xy(target("dx.Texture", <4 x float>, 0, 0, 1, 5) %handle, i32 %mip)
  ret <3 x i32> %dims
}

; CHECK-LABEL: define <3 x i32> @array2d_base(
; CHECK: [[D:%.*]] = call <3 x i32> @llvm.dx.resource.getdimensions.xyz{{.*}}(target("dx.Texture", i32, 1, 0, 0, 7) {{.*}})
; CHECK: extractelement <3 x i32> [[D]], i32 0
; CHECK: extractelement <3 x i32> [[D]], i32 1
; CHECK: extractelement <3 x i32> [[D]], i32 2
; CHECK-NOT: @dx.op.getDimensions
define <3 x i32> @array2d_base() {
  %handle = call target("dx.Texture", i32, 1, 0, 0, 7) @llvm.dx.resource.handlefrombinding(i32 0, i32 6, i32 1, i32 0, ptr null)
  %dims = call <3 x i32> @llvm.dx.resource.getdimensions.xyz(target("dx.Texture", i32, 1, 0, 0, 7) %handle)
  ret <3 x i32> %dims
}

; CHECK-LABEL: define <4 x i32> @array2d_dynamic(
; CHECK: [[D:%.*]] = call <4 x i32> @llvm.dx.resource.getdimensions.levels.xyz{{.*}}(target("dx.Texture", <4 x float>, 0, 0, 1, 7) {{.*}}, i32 %mip)
; CHECK: extractelement <4 x i32> [[D]], i32 0
; CHECK: extractelement <4 x i32> [[D]], i32 1
; CHECK: extractelement <4 x i32> [[D]], i32 2
; CHECK: extractelement <4 x i32> [[D]], i32 3
; CHECK-NOT: @dx.op.getDimensions
define <4 x i32> @array2d_dynamic(i32 %mip) {
  %handle = call target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefrombinding(i32 0, i32 7, i32 1, i32 0, ptr null)
  %dims = call <4 x i32> @llvm.dx.resource.getdimensions.levels.xyz(target("dx.Texture", <4 x float>, 0, 0, 1, 7) %handle, i32 %mip)
  ret <4 x i32> %dims
}

; CHECK-LABEL: define i32 @array2d_count_only(
; CHECK: [[D:%.*]] = call <4 x i32> @llvm.dx.resource.getdimensions.levels.xyz{{.*}}({{.*}}, i32 0)
; CHECK: extractelement <4 x i32> [[D]], i32 3
; CHECK-NOT: @dx.op.getDimensions
define i32 @array2d_count_only() {
  %handle = call target("dx.Texture", i32, 0, 0, 1, 7) @llvm.dx.resource.handlefrombinding(i32 0, i32 8, i32 1, i32 0, ptr null)
  %dims = call <4 x i32> @llvm.dx.resource.getdimensions.levels.xyz(target("dx.Texture", i32, 0, 0, 1, 7) %handle, i32 undef)
  %count = extractelement <4 x i32> %dims, i32 3
  ret i32 %count
}

; CHECK-LABEL: define <3 x i32> @volume_base(
; CHECK: [[D:%.*]] = call <3 x i32> @llvm.dx.resource.getdimensions.xyz{{.*}}(target("dx.Texture", i32, 1, 0, 0, 4) {{.*}})
; CHECK: extractelement <3 x i32> [[D]], i32 0
; CHECK: extractelement <3 x i32> [[D]], i32 1
; CHECK: extractelement <3 x i32> [[D]], i32 2
; CHECK-NOT: @dx.op.getDimensions
define <3 x i32> @volume_base() {
  %handle = call target("dx.Texture", i32, 1, 0, 0, 4) @llvm.dx.resource.handlefrombinding(i32 0, i32 9, i32 1, i32 0, ptr null)
  %dims = call <3 x i32> @llvm.dx.resource.getdimensions.xyz(target("dx.Texture", i32, 1, 0, 0, 4) %handle)
  ret <3 x i32> %dims
}

; CHECK-LABEL: define <4 x i32> @volume_dynamic(
; CHECK: [[D:%.*]] = call <4 x i32> @llvm.dx.resource.getdimensions.levels.xyz{{.*}}(target("dx.Texture", <4 x float>, 0, 0, 1, 4) {{.*}}, i32 %mip)
; CHECK: extractelement <4 x i32> [[D]], i32 0
; CHECK: extractelement <4 x i32> [[D]], i32 1
; CHECK: extractelement <4 x i32> [[D]], i32 2
; CHECK: extractelement <4 x i32> [[D]], i32 3
; CHECK-NOT: @dx.op.getDimensions
define <4 x i32> @volume_dynamic(i32 %mip) {
  %handle = call target("dx.Texture", <4 x float>, 0, 0, 1, 4) @llvm.dx.resource.handlefrombinding(i32 0, i32 10, i32 1, i32 0, ptr null)
  %dims = call <4 x i32> @llvm.dx.resource.getdimensions.levels.xyz(target("dx.Texture", <4 x float>, 0, 0, 1, 4) %handle, i32 %mip)
  ret <4 x i32> %dims
}

; CHECK-LABEL: define i32 @volume_count_only(
; CHECK: [[D:%.*]] = call <4 x i32> @llvm.dx.resource.getdimensions.levels.xyz{{.*}}({{.*}}, i32 0)
; CHECK: extractelement <4 x i32> [[D]], i32 3
; CHECK-NOT: @dx.op.getDimensions
define i32 @volume_count_only() {
  %handle = call target("dx.Texture", i32, 0, 0, 1, 4) @llvm.dx.resource.handlefrombinding(i32 0, i32 11, i32 1, i32 0, ptr null)
  %dims = call <4 x i32> @llvm.dx.resource.getdimensions.levels.xyz(target("dx.Texture", i32, 0, 0, 1, 4) %handle, i32 undef)
  %count = extractelement <4 x i32> %dims, i32 3
  ret i32 %count
}
