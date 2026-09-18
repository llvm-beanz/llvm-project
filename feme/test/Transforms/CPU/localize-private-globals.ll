; RUN: feme-opt --llvm -passes=feme-cpu-localize-private-globals -S %s | FileCheck %s

; feme::cpu::LocalizePrivateGlobalsPass in isolation: a scalar,
; address-space-0, non-constant global used by exactly one function is
; replaced by a real local `alloca` in that function's entry block, with
; its (non-`undef`) initializer preserved as a seeding store. A global
; that is constant or used by more than one function is left alone --
; see the pass's own header comment for why. Roadmap L116(a)/C8b
; broadened this pass to also localize a struct/array-typed global whose
; every leaf is a scalar or fixed-vector type, as long as no leaf is a
; non-power-of-2-width vector (which would interact with
; `feme::cpu::LocalNarrowVectorArrayInitPass`'s own tight-offset fixup).

; CHECK-NOT: @scalarNoInit
; CHECK-NOT: @scalarWithInit
; A `constant` global (e.g. a debug-name string literal) is never
; per-invocation storage and must be left as-is.
; CHECK: @aConstant = private constant [4 x i8] c"abc\00"
; A global used by more than one function is not safely localizable to
; either one alone and must be left as-is.
; CHECK: @sharedByTwoFunctions = private global float undef
; A scalar-leaf array is now localized (roadmap L116(a)/C8b): `float`'s
; own natural and tight sizes trivially coincide, so there is no
; padding-gap risk at all.
; CHECK-NOT: @anArray
; A `<2 x float>`-leaf array is also localized: 2 is a power of 2, so its
; tight and ABI-padded sizes coincide -- exactly the
; `dEQP-VK.glsl.linkage.varying.struct.mat4x2` shape this broadening was
; added to close.
; CHECK-NOT: @vec2Array
; A `<3 x float>`-leaf array is deliberately still left alone: 3 is not a
; power of 2, so its ABI-padded size (16 bytes) disagrees with its
; tightly packed size (12 bytes) -- exactly the shape
; `feme::cpu::LocalNarrowVectorArrayInitPass` already owns; localizing it
; here could reintroduce the "write one layout, read a different one"
; corruption that pass exists to prevent.
; CHECK: @vec3Array = private global [2 x <3 x float>] zeroinitializer
; A scalar-leaf struct is also localized.
; CHECK-NOT: @aStruct
; An `int`-array global used by a function that may `discard`/`demote`
; is now also localized (roadmap L122): this pass previously excluded
; any aggregate global whose one using function could ever discard or
; demote, working around a `feme::cpu::SIMDizePass` bug (roadmap L118)
; that has since been fixed at its own root (`widenMaskedStore` now uses
; `Env.EntryMask`, not `Env.SideEffectMask`, for a `MaskedAllocas`-based
; destination); a full `graphicsfuzz.*` CTS re-sweep with this guard
; removed confirmed 0 regressions relative to the guard-enabled
; baseline, so the guard itself was removed as no longer necessary.
; CHECK-NOT: @arrayInDiscardingFunction

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

; CHECK-LABEL: define void @usesArray(
; CHECK: %anArray = alloca [4 x float]
; CHECK-NEXT: store [4 x float] zeroinitializer, ptr %anArray
; CHECK-NEXT: %v = load float, ptr %anArray
@anArray = private global [4 x float] zeroinitializer
define void @usesArray(ptr %out) {
  %v = load float, ptr @anArray
  store float %v, ptr %out
  ret void
}

; CHECK-LABEL: define void @usesVec2Array(
; CHECK: %vec2Array = alloca [4 x <2 x float>]
; CHECK-NEXT: store [4 x <2 x float>] zeroinitializer, ptr %vec2Array
; CHECK-NEXT: %v = load <2 x float>, ptr %vec2Array
@vec2Array = private global [4 x <2 x float>] zeroinitializer
define void @usesVec2Array(ptr %out) {
  %v = load <2 x float>, ptr @vec2Array
  store <2 x float> %v, ptr %out
  ret void
}

; CHECK-LABEL: define void @usesVec3Array(
; CHECK: %v = load <3 x float>, ptr @vec3Array
@vec3Array = private global [2 x <3 x float>] zeroinitializer
define void @usesVec3Array(ptr %out) {
  %v = load <3 x float>, ptr @vec3Array
  store <3 x float> %v, ptr %out
  ret void
}

; CHECK-LABEL: define void @usesStruct(
; CHECK: %aStruct = alloca { float, i32 }
; CHECK-NEXT: store { float, i32 } zeroinitializer, ptr %aStruct
; CHECK-NEXT: %v = load float, ptr %aStruct
@aStruct = private global { float, i32 } zeroinitializer
define void @usesStruct(ptr %out) {
  %v = load float, ptr @aStruct
  store float %v, ptr %out
  ret void
}

; CHECK-LABEL: define void @usesArrayInDiscardingFunction(
; CHECK: %arrayInDiscardingFunction = alloca [4 x i32]
; CHECK-NEXT: store [4 x i32] zeroinitializer, ptr %arrayInDiscardingFunction
; CHECK: %v = load i32, ptr %arrayInDiscardingFunction
@arrayInDiscardingFunction = private global [4 x i32] zeroinitializer
define void @usesArrayInDiscardingFunction(i1 %cond, ptr %out) {
  call void @feme.stage.discard(i1 %cond)
  %v = load i32, ptr @arrayInDiscardingFunction
  store i32 %v, ptr %out
  ret void
}
declare void @feme.stage.discard(i1)


