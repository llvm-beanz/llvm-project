; RUN: feme-opt --llvm -passes=feme-cpu-localize-private-globals -S %s | FileCheck %s

; feme::cpu::LocalizePrivateGlobalsPass in isolation: a scalar,
; address-space-0, non-constant global used by exactly one function is
; replaced by a real local `alloca` in that function's entry block, with
; its (non-`undef`) initializer preserved as a seeding store. A global
; that is constant, used by more than one function, or array/struct
; -typed is left alone -- see the pass's own header comment for why each
; is excluded.

; CHECK-NOT: @scalarNoInit
; CHECK-NOT: @scalarWithInit
; A `constant` global (e.g. a debug-name string literal) is never
; per-invocation storage and must be left as-is.
; CHECK: @aConstant = private constant [4 x i8] c"abc\00"
; A global used by more than one function is not safely localizable to
; either one alone and must be left as-is.
; CHECK: @sharedByTwoFunctions = private global float undef
; An array-typed global is deliberately left alone (see the pass's own
; header comment for why: array/struct-typed globals are out of scope
; for now, to avoid interacting with
; `feme::cpu::LocalNarrowVectorArrayInitPass`'s own array-specific fixup).
; CHECK: @anArray = private global [4 x float] zeroinitializer

; CHECK-LABEL: define void @usesScalarNoInit(
; CHECK: %scalarNoInit = alloca float
; CHECK-NEXT: %v = load float, ptr %scalarNoInit
@scalarNoInit = private global float undef
define void @usesScalarNoInit(ptr %out) {
  %v = load float, ptr @scalarNoInit
  store float %v, ptr %out
  ret void
}

; CHECK-LABEL: define void @usesScalarWithInit(
; CHECK: %scalarWithInit = alloca float
; CHECK-NEXT: store float 4.200000e+01, ptr %scalarWithInit
; CHECK-NEXT: %v = load float, ptr %scalarWithInit
@scalarWithInit = private global float 4.200000e+01
define void @usesScalarWithInit(ptr %out) {
  %v = load float, ptr @scalarWithInit
  store float %v, ptr %out
  ret void
}

@aConstant = private constant [4 x i8] c"abc\00"
define void @usesConstant(ptr %out) {
  store ptr @aConstant, ptr %out
  ret void
}

@sharedByTwoFunctions = private global float undef
define void @writer(float %v) {
  store float %v, ptr @sharedByTwoFunctions
  ret void
}
define float @reader() {
  %v = load float, ptr @sharedByTwoFunctions
  ret float %v
}

@anArray = private global [4 x float] zeroinitializer
define void @usesArray(ptr %out) {
  %v = load float, ptr @anArray
  store float %v, ptr %out
  ret void
}
