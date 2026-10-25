; RUN: split-file %s %t
; RUN: cat %t/common.ll %t/unsupported.ll > %t/input.ll
; RUN: echo "existing output" > %t/output.dxil
; RUN: not dxil-as --filetype=dxcontainer %t/input.ll -o %t/output.dxil 2>&1 | FileCheck %s --check-prefix=PROPERTYTAG
; RUN: not dxil-as --filetype=dxcontainer --disable-output %t/input.ll -o %t/output.dxil 2>&1 | FileCheck %s --check-prefix=PROPERTYTAG
; RUN: not dxil-as --filetype=dxcontainer --disable-verify %t/input.ll -o %t/output.dxil 2>&1 | FileCheck %s --check-prefix=PROPERTYTAG
; RUN: FileCheck %s --check-prefix=UNCHANGED --input-file=%t/output.dxil
; RUN: cat %t/common.ll %t/duplicate.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=DUPLICATE
; RUN: cat %t/common.ll %t/odd.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=ODD
; RUN: cat %t/common.ll %t/missing.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=MISSING
; RUN: cat %t/common.ll %t/product.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=PRODUCT
; RUN: cat %t/common.ll %t/zero.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=ZERO
; RUN: cat %t/common.ll %t/dimensions.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=DIMENSIONS
; RUN: cat %t/common.ll %t/flag-width.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=FLAGWIDTH
; RUN: cat %t/common.ll %t/flag-bits.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=FLAGBITS
; RUN: cat %t/common.ll %t/wave.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=WAVE
; RUN: sed 's/!"cs"/!"ps"/' %S/compute-container.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=STAGE
; RUN: sed 's/i32 6, i32 0}/i32 6, i32 8}/' %S/compute-container.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=MODEL
; RUN: sed 's/!0 = !{i32 1, i32 0}/!0 = !{i32 1, i32 1}/' %S/compute-container.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=DXIL
; RUN: sed 's/!1 = !{i32 1, i32 8}/!1 = !{i32 1, i32 10}/' %S/compute-container.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=VALIDATOR
; RUN: sed '/^!dx.valver =/d' %S/compute-container.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=NOVALIDATOR
; RUN: sed 's/!dx.entryPoints = !{!7}/!dx.entryPoints = !{!7, !7}/' %S/compute-container.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=ENTRIES
; RUN: sed 's/!3 = !{null, !4, null, null}/!3 = !{null}/' %S/compute-container.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=RESOURCES
; RUN: sed 's/i32 1, i32 4}/i32 99, i32 4}/' %S/compute-container.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=PROPERTY
; RUN: sed 's/!7 = !{void ()\* @main/!7 = !{null/' %S/compute-container.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=ENTRY
; RUN: sed 's/!"main", null, !3/!"main", !9, !3/' %S/compute-container.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=SIGNATURE
; RUN: sed 's/i32 0, i32 0, i32 1, i32 12/i32 0, i32 3, i32 -2, i32 12/' %S/compute-container.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=RANGE
; RUN: sed 's/i32 0, i32 0, i32 1, i32 12/i32 0, i32 0, i32 0, i32 12/' %S/compute-container.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=RANGE
; RUN: cat %t/common.ll %t/root.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=ROOT
; RUN: cat %t/common.ll %t/debug.ll | not dxil-as --filetype=dxcontainer 2>&1 | FileCheck %s --check-prefix=DEBUG

; PROPERTYTAG: cannot emit DXContainer: unsupported entry property tag 12
; UNCHANGED: {{^existing output$}}
; DUPLICATE: cannot emit DXContainer: duplicate entry property
; ODD: cannot emit DXContainer: entry properties must be tag/value pairs
; MISSING: cannot emit DXContainer: missing numthreads entry property
; PRODUCT: cannot emit DXContainer: numthreads product exceeds 1024
; ZERO: cannot emit DXContainer: invalid numthreads dimension
; DIMENSIONS: cannot emit DXContainer: numthreads must be a tuple with 3 operands
; FLAGWIDTH: cannot emit DXContainer: shader flags must be an i64 constant
; FLAGBITS: cannot emit DXContainer: unsupported shader flags
; WAVE: cannot emit DXContainer: unsupported wave size
; STAGE: cannot emit DXContainer: only compute shaders are supported
; MODEL: cannot emit DXContainer: supported shader models are cs_6_0 through cs_6_7
; DXIL: cannot emit DXContainer: DXIL version must match the shader model
; VALIDATOR: cannot emit DXContainer: unsupported dx.valver version
; NOVALIDATOR: cannot emit DXContainer: dx.valver must contain exactly one tuple
; ENTRIES: cannot emit DXContainer: dx.entryPoints must contain exactly one tuple
; RESOURCES: cannot emit DXContainer: dx.resources must be a tuple with 4 operands
; PROPERTY: cannot emit DXContainer: unsupported or duplicate resource property
; ENTRY: cannot emit DXContainer: entry point must name a defined function
; SIGNATURE: cannot emit DXContainer: compute signatures must be empty
; RANGE: cannot emit DXContainer: invalid resource binding range
; ROOT: cannot emit DXContainer: unsupported container metadata: dx.rootSignature
; DEBUG: cannot emit DXContainer: unsupported container metadata: llvm.dbg.test

;--- common.ll
define void @main() { ret void }
!dx.version = !{!0}
!dx.valver = !{!1}
!dx.shaderModel = !{!2}
!dx.entryPoints = !{!3}
!0 = !{i32 1, i32 0}
!1 = !{i32 1, i32 8}
!2 = !{!"cs", i32 6, i32 0}
!3 = !{void ()* @main, !"main", null, null, !4}
;--- unsupported.ll
!4 = !{i32 12, !5}
!5 = !{i32 8, i32 2, i32 1}
;--- duplicate.ll
!4 = !{i32 0, i64 16, i32 0, i64 0}
;--- odd.ll
!4 = !{i32 0}
;--- missing.ll
!4 = !{i32 0, i64 16}
;--- product.ll
!4 = !{i32 4, !5}
!5 = !{i32 1024, i32 2, i32 1}
;--- zero.ll
!4 = !{i32 4, !5}
!5 = !{i32 0, i32 2, i32 1}
;--- dimensions.ll
!4 = !{i32 4, !5}
!5 = !{i64 8}
;--- flag-width.ll
!4 = !{i32 0, i32 16}
;--- flag-bits.ll
!4 = !{i32 0, i64 1099511627776, i32 4, !5}
!5 = !{i32 8, i32 2, i32 1}
;--- wave.ll
!4 = !{i32 4, !5, i32 11, !6}
!5 = !{i32 8, i32 2, i32 1}
!6 = !{i32 32}
;--- root.ll
!4 = !{i32 4, !5}
!5 = !{i32 8, i32 2, i32 1}
!dx.rootSignature = !{!0}
;--- debug.ll
!4 = !{i32 4, !5}
!5 = !{i32 8, i32 2, i32 1}
!llvm.dbg.test = !{!0}
!llvm.module.flags = !{!6}
!6 = !{i32 2, !"Debug Info Version", i32 3}
