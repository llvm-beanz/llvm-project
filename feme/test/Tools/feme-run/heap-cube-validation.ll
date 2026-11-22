; RUN: split-file %s %t
; RUN: not feme-run --reference --heap=%t/rectangle.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=SQUARE
; RUN: not feme-run --reference --heap=%t/empty.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=SQUARE
; RUN: not feme-run --reference --heap=%t/layers.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=LAYERS
; RUN: not feme-run --reference --heap=%t/zero-layers.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=LAYERS
; RUN: not feme-run --reference --heap=%t/overflow.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=OVERFLOW
; RUN: not feme-run --reference --heap=%t/host-limit.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=HOST-LIMIT
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; SQUARE: heap entry image 0 requires nonzero square cube face extents
; LAYERS: heap entry image 0 cube-array requires 'array-layers' to be a positive multiple of 6
; OVERFLOW: heap entry image 0 subresource size overflows
; HOST-LIMIT: heap entry image 0 subresource size exceeds host allocation limits
;
;--- shader.ll
define void @main() #0 {
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="1,1,1" }
;--- rectangle.yaml
images:
  - index: 0
    dimension: cube
    extent: [2, 1]
    format: r32_float
;--- empty.yaml
images:
  - index: 0
    dimension: cube
    extent: [0, 0]
    format: r32_float
;--- layers.yaml
images:
  - index: 0
    dimension: cube-array
    array-layers: 7
    extent: [1, 1]
    format: r32_float
;--- zero-layers.yaml
images:
  - index: 0
    dimension: cube-array
    array-layers: 0
    extent: [1, 1]
    format: r32_float
;--- overflow.yaml
images:
  - index: 0
    dimension: cube
    extent: [1073741824, 1073741824]
    format: r32_float
;--- host-limit.yaml
images:
  - index: 0
    dimension: cube
    extent: [805306368, 805306368]
    format: r32_float
