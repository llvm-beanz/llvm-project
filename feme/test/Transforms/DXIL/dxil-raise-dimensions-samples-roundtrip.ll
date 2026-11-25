; RUN: opt -S -dxil-op-lower %s | feme-opt --llvm -passes=feme-dxil-raise-ops,verify -S | FileCheck %s
; RUN: opt -S -dxil-op-lower %s | feme-opt --llvm -passes=feme-dxil-raise-ops,feme-dxil-raise-ops,verify -S | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK-LABEL: define <3 x i32> @plain(
; CHECK: [[D:%.*]] = call <3 x i32> @llvm.dx.resource.getdimensions.samples.xy
; CHECK: extractelement <3 x i32> [[D]], i32 0
; CHECK: extractelement <3 x i32> [[D]], i32 1
; CHECK: extractelement <3 x i32> [[D]], i32 2
; CHECK-NOT: @dx.op.getDimensions
define <3 x i32> @plain() {
  %t = call target("dx.MSTexture", <4 x float>, 0, 4, 0, 3) @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %d = call <3 x i32> @llvm.dx.resource.getdimensions.samples.xy(target("dx.MSTexture", <4 x float>, 0, 4, 0, 3) %t)
  ret <3 x i32> %d
}

; CHECK-LABEL: define <4 x i32> @array(
; CHECK: [[D:%.*]] = call <4 x i32> @llvm.dx.resource.getdimensions.samples.xyz
; CHECK: extractelement <4 x i32> [[D]], i32 0
; CHECK: extractelement <4 x i32> [[D]], i32 1
; CHECK: extractelement <4 x i32> [[D]], i32 2
; CHECK: extractelement <4 x i32> [[D]], i32 3
; CHECK-NOT: @dx.op.getDimensions
define <4 x i32> @array() {
  %t = call target("dx.MSTexture", i32, 0, 2, 1, 8) @llvm.dx.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
  %d = call <4 x i32> @llvm.dx.resource.getdimensions.samples.xyz(target("dx.MSTexture", i32, 0, 2, 1, 8) %t)
  ret <4 x i32> %d
}

; CHECK-LABEL: define i32 @samples_only(
; CHECK: [[D:%.*]] = call <3 x i32> @llvm.dx.resource.getdimensions.samples.xy
; CHECK: extractelement <3 x i32> [[D]], i32 2
; CHECK-NOT: @dx.op.getDimensions
define i32 @samples_only() {
  %t = call target("dx.MSTexture", float, 0, 0, 0, 3) @llvm.dx.resource.handlefrombinding(i32 0, i32 2, i32 1, i32 0, ptr null)
  %d = call <3 x i32> @llvm.dx.resource.getdimensions.samples.xy(target("dx.MSTexture", float, 0, 0, 0, 3) %t)
  %s = extractelement <3 x i32> %d, i32 2
  ret i32 %s
}

; CHECK-LABEL: define i32 @layers_only(
; CHECK: [[D:%.*]] = call <3 x i32> @llvm.dx.resource.getdimensions.xyz
; CHECK: extractelement <3 x i32> [[D]], i32 2
; CHECK-NOT: @dx.op.getDimensions
define i32 @layers_only() {
  %t = call target("dx.MSTexture", i32, 0, 2, 1, 8) @llvm.dx.resource.handlefrombinding(i32 0, i32 3, i32 1, i32 0, ptr null)
  %d = call <3 x i32> @llvm.dx.resource.getdimensions.xyz(target("dx.MSTexture", i32, 0, 2, 1, 8) %t)
  %l = extractelement <3 x i32> %d, i32 2
  ret i32 %l
}

; CHECK-LABEL: define <3 x i32> @nonzero_mip(
; CHECK: call %dx.types.Dimensions @dx.op.getDimensions({{.*}}, i32 1)
; CHECK-NOT: call {{.*}} @llvm.dx.resource.getdimensions
define <3 x i32> @nonzero_mip() {
  %t = call target("dx.MSTexture", float, 0, 4, 0, 3) @llvm.dx.resource.handlefrombinding(i32 0, i32 4, i32 1, i32 0, ptr null)
  %d = call <3 x i32> @llvm.dx.resource.getdimensions.levels.xy(target("dx.MSTexture", float, 0, 4, 0, 3) %t, i32 1)
  ret <3 x i32> %d
}

; CHECK-LABEL: define <3 x i32> @plain_third_field(
; CHECK: call %dx.types.Dimensions @dx.op.getDimensions
; CHECK-NOT: call {{.*}} @llvm.dx.resource.getdimensions
define <3 x i32> @plain_third_field() {
  %t = call target("dx.MSTexture", float, 0, 4, 0, 3) @llvm.dx.resource.handlefrombinding(i32 0, i32 5, i32 1, i32 0, ptr null)
  %d = call <3 x i32> @llvm.dx.resource.getdimensions.xyz(target("dx.MSTexture", float, 0, 4, 0, 3) %t)
  ret <3 x i32> %d
}
