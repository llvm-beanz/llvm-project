; RUN: not feme-run %s 2>&1 | FileCheck %s
; RUN: not feme-run --reference %s 2>&1 | FileCheck %s
; RUN: llvm-as %s -o %t.bc
; RUN: not feme --target=%feme_host_triple %t.bc -o %t.o 2>&1 | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; Plain 1D has no second dimension; a pair query must not invent one.
; CHECK: requires heap-backed FP32/i32 1D/array or plain 2D mip-zero texture dimension queries
define void @main() #0 {
  %t = call target("dx.Texture", float, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.p(i32 0)
  %d = call <2 x i32> @llvm.dx.resource.getdimensions.xy.p(target("dx.Texture", float, 0, 0, 0, 1) %t)
  call void @consume(<2 x i32> %d)
  ret void
}
declare target("dx.Texture", float, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.p(i32)
declare <2 x i32> @llvm.dx.resource.getdimensions.xy.p(target("dx.Texture", float, 0, 0, 0, 1))
declare void @consume(<2 x i32>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="5,1,1" }
