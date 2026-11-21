; RUN: split-file %s %t
; RUN: not feme-run --root-signature-constants %t/default.ll 2>&1 | FileCheck %s --check-prefix=DEFAULT
; RUN: not feme-run --root-signature-constants --reference %t/default.ll 2>&1 | FileCheck %s --check-prefix=DEFAULT
; RUN: llvm-as %t/default.ll -o %t/default.bc
; RUN: not feme --root-signature-constants --target=%feme_host_triple %t/default.bc -o %t/rejected.o 2>&1 | FileCheck %s --check-prefix=DEFAULT
; RUN: not feme-run --root-signature-constants %t/comparison.ll 2>&1 | FileCheck %s --check-prefix=COMPARISON
; RUN: not feme-run --root-signature-constants --reference %t/comparison.ll 2>&1 | FileCheck %s --check-prefix=COMPARISON
; RUN: llvm-as %t/comparison.ll -o %t/comparison.bc
; RUN: not feme --root-signature-constants --target=%feme_host_triple %t/comparison.bc -o %t/rejected.o 2>&1 | FileCheck %s --check-prefix=COMPARISON
; RUN: not feme-run --root-signature-constants %t/maximum.ll 2>&1 | FileCheck %s --check-prefix=MAXIMUM
; RUN: not feme-run --root-signature-constants --reference %t/maximum.ll 2>&1 | FileCheck %s --check-prefix=MAXIMUM
; RUN: llvm-as %t/maximum.ll -o %t/maximum.bc
; RUN: not feme --root-signature-constants --target=%feme_host_triple %t/maximum.bc -o %t/rejected.o 2>&1 | FileCheck %s --check-prefix=MAXIMUM
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; Keep canonical sampler handles live without claiming supported sampling.
; Each root has one b3/space7 constant and one s3/space7 static sampler.
; DEFAULT: static sampler mismatch: sampler space=7 register=3 in 'main' declares mode=0 but filter=128 requires mode=1
; COMPARISON: static sampler mismatch: sampler space=7 register=3 in 'main' declares mode=1 but filter=0 requires mode=0
; MAXIMUM: static sampler mismatch: sampler space=7 register=3 in 'main' declares mode=1 but filter=384 requires mode=0

;--- default.ll
define void @main() #0 !feme.dxil.rootsignature !0 {
  %h = call target("dx.Sampler", 0) @llvm.dx.resource.handlefrombinding(i32 7, i32 3, i32 1, i32 0, ptr null)
  call void @consume(target("dx.Sampler", 0) %h)
  ret void
}
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
declare void @consume(target("dx.Sampler", 0))
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="1,1,1" }
!0 = !{[100 x i8] c"\02\00\00\00\01\00\00\00\18\00\00\00\01\00\00\00\30\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\24\00\00\00\03\00\00\00\07\00\00\00\01\00\00\00\80\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\10\00\00\00\04\00\00\00\02\00\00\00\00\00\00\00\FF\FF\7F\7F\03\00\00\00\07\00\00\00\00\00\00\00"}

;--- comparison.ll
define void @main() #0 !feme.dxil.rootsignature !0 {
  %h = call target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding(i32 7, i32 3, i32 1, i32 0, ptr null)
  call void @consume(target("dx.Sampler", 1) %h)
  ret void
}
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
declare void @consume(target("dx.Sampler", 1))
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="1,1,1" }
!0 = !{[100 x i8] c"\02\00\00\00\01\00\00\00\18\00\00\00\01\00\00\00\30\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\24\00\00\00\03\00\00\00\07\00\00\00\01\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\10\00\00\00\04\00\00\00\02\00\00\00\00\00\00\00\FF\FF\7F\7F\03\00\00\00\07\00\00\00\00\00\00\00"}

;--- maximum.ll
define void @main() #0 !feme.dxil.rootsignature !0 {
  %h = call target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding(i32 7, i32 3, i32 1, i32 0, ptr null)
  call void @consume(target("dx.Sampler", 1) %h)
  ret void
}
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
declare void @consume(target("dx.Sampler", 1))
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="1,1,1" }
!0 = !{[100 x i8] c"\02\00\00\00\01\00\00\00\18\00\00\00\01\00\00\00\30\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\24\00\00\00\03\00\00\00\07\00\00\00\01\00\00\00\80\01\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\10\00\00\00\04\00\00\00\02\00\00\00\00\00\00\00\FF\FF\7F\7F\03\00\00\00\07\00\00\00\00\00\00\00"}
