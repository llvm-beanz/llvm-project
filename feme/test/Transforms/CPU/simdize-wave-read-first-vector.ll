; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | opt -S -passes=verify | FileCheck %s
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <2 x i32>>
; CHECK: call i32 @feme.cpu.wave.readfirst.i32.v4(<4 x i1> %wave_entry_mask, <4 x i32> %{{.*}})
; CHECK: insertelement <2 x i32> poison, i32 %{{.*}}, i32 0
; CHECK: call i32 @feme.cpu.wave.readfirst.i32.v4(<4 x i1> %wave_entry_mask, <4 x i32> %{{.*}})
; CHECK: insertelement <2 x i32> %{{.*}}, i32 %{{.*}}, i32 1
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %lane = call i32 @llvm.dx.wave.getlaneindex()
  %v0 = insertelement <2 x i32> poison, i32 %tid, i32 0
  %v1 = insertelement <2 x i32> %v0, i32 %lane, i32 1
  %first = call <2 x i32> @llvm.dx.wave.readlane.first.v2i32(<2 x i32> %v1)
  %e0 = extractelement <2 x i32> %first, i32 0
  %e1 = extractelement <2 x i32> %first, i32 1
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.wave.getlaneindex()
declare <2 x i32> @llvm.dx.wave.readlane.first.v2i32(<2 x i32>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
