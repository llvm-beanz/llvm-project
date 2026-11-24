; RUN: split-file %s %t
; RUN: not feme-run --heap=%t/zero.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=MIPS
; RUN: not feme-run --reference --heap=%t/too-many.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=MIPS
; RUN: not feme-run --heap=%t/ms.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=MIPS
; RUN: not feme-run --heap=%t/ms-array.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=MIPS
; RUN: not feme-run --heap=%t/width.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=EXTENT
; RUN: not feme-run --heap=%t/height.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=EXTENT
; RUN: not feme-run --heap=%t/depth.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=EXTENT
; RUN: not feme-run --heap=%t/subresource-overflow.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=SUBRESOURCE
; RUN: not feme-run --heap=%t/chain-overflow.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=CHAIN
; RUN: not feme-run --heap=%t/host-limit.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=HOST
; RUN: not feme-run --heap=%t/bound-zero.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=MIPS
; RUN: feme-run --repeat=2 --heap=%t/bound.yaml %t/shader.ll | FileCheck %s --check-prefix=BOUND
; RUN: feme-run --reference --repeat=2 --heap=%t/bound.yaml %t/shader.ll | FileCheck %s --check-prefix=BOUND
; RUN: llvm-as %t/shader.ll -o %t/shader.bc
; RUN: feme --target=%feme_host_triple %t/shader.bc -o %t/shader.o
; RUN: not feme-run --object --heap=%t/too-many.yaml %t/shader.o 2>&1 | FileCheck %s --check-prefix=MIPS
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; MIPS: heap entry image 0 requires 'mip-levels' in [1, 32] (1 for MS dimensions)
; EXTENT: heap entry image 0 mip chains require nonzero extents
; SUBRESOURCE: heap entry image 0 subresource size overflows
; CHAIN: heap entry image 0 mip-chain size overflows
; HOST: heap entry image 0 subresource size exceeds host allocation limits
; BOUND: image-binding[0:0][0]: 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 0
;--- shader.ll
define void @main() #0 {
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="1,1,1" }
;--- zero.yaml
images:
  - {index: 0, dimension: 2d, mip-levels: 0, format: r32_uint}
;--- too-many.yaml
images:
  - {index: 0, dimension: 1d, mip-levels: 33, format: r32_uint}
;--- ms.yaml
images:
  - {index: 0, dimension: 2d-ms, sample-count: 4, mip-levels: 2, format: r32_uint}
;--- ms-array.yaml
images:
  - {index: 0, dimension: 2d-ms-array, array-layers: 2, mip-levels: 2, format: r32_uint}
;--- width.yaml
images:
  - {index: 0, dimension: 1d, extent: [0], mip-levels: 2, format: r32_uint}
;--- height.yaml
images:
  - {index: 0, dimension: 2d, extent: [2, 0], mip-levels: 2, format: r32_uint}
;--- depth.yaml
images:
  - {index: 0, dimension: 3d, extent: [2, 2, 0], mip-levels: 2, format: r32_uint}
;--- subresource-overflow.yaml
images:
  - {index: 0, dimension: 2d-array, extent: [1073741824, 1073741824], array-layers: 4, mip-levels: 2, format: r32_uint}
;--- chain-overflow.yaml
images:
  - {index: 0, dimension: 2d, extent: [2013265920, 2013265920], mip-levels: 2, format: r32_uint}
;--- host-limit.yaml
images:
  - {index: 0, dimension: 2d, extent: [1610612736, 1610612736], mip-levels: 2, format: r32_uint}
;--- bound-zero.yaml
image-bindings:
  - space: 0
    register: 0
    entries:
      - {index: 0, dimension: 2d, mip-levels: 0, format: r32_uint}
;--- bound.yaml
image-bindings:
  - space: 0
    register: 0
    entries:
      - {index: 0, dimension: 2d, extent: [5, 3], mip-levels: 3, format: r32_uint, data: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17]}
