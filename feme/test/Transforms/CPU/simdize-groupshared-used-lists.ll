; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; RUN: feme-opt --llvm -passes=feme-cpu-simdize,verify -feme-cpu-wave-size=4 -S %s | FileCheck %s

@shared = internal addrspace(3) global [4 x i32] undef, align 4
@keep = internal global i32 0
@llvm.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(3) @shared to ptr), ptr @keep], section "llvm.metadata"
@llvm.compiler.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(3) @shared to ptr), ptr @keep], section "llvm.metadata"

; CHECK: @llvm.used = appending global [1 x ptr] [ptr @keep]
; CHECK: @llvm.compiler.used = appending global [1 x ptr] [ptr @keep]
; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK: getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK: call <4 x i32> @llvm.masked.gather.v4i32.v4p0
; CHECK-NOT: ptr addrspace(3)
; CHECK: ret void
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
  %address = getelementptr [4 x i32], ptr addrspace(3) @shared, i32 0, i32 %tid
  %value = load i32, ptr addrspace(3) %address, align 4
  ret void
}

declare i32 @llvm.dx.thread.id.in.group(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
