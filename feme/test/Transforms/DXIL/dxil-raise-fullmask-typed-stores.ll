; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes='feme-dxil-raise-ops,verify,feme-dxil-raise-ops,verify' -S %s | FileCheck %s

target triple = "dxil-ms-dx"
%dx.types.Handle = type { ptr }

; Full masks preserve all channels, even through a scalar shader handle.
; CHECK-LABEL: define void @scalar_i32(
define void @scalar_i32(i32 %index, i32 %x, i32 %y, i32 %z, i32 %w) {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.i32(target("dx.TypedBuffer", i32, 1, 0, 0) poison)
  ; CHECK: [[V0:%.*]] = insertelement <4 x i32> poison, i32 %x, i32 0
  ; CHECK: [[V1:%.*]] = insertelement <4 x i32> [[V0]], i32 %y, i32 1
  ; CHECK: [[V2:%.*]] = insertelement <4 x i32> [[V1]], i32 %z, i32 2
  ; CHECK: [[V3:%.*]] = insertelement <4 x i32> [[V2]], i32 %w, i32 3
  ; CHECK: call void @llvm.dx.resource.store.typedbuffer{{.*}}(target("dx.TypedBuffer", i32, 1, 0, 0) poison, i32 %index, <4 x i32> [[V3]])
  ; CHECK-NOT: call void @dx.op.bufferStore
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %h, i32 %index, i32 undef, i32 %x, i32 %y, i32 %z, i32 %w, i8 15)
  ret void
}

; CHECK-LABEL: define void @float_widths(
define void @float_widths(float %x, float %y, float %z, float %w) {
  %h1 = call %dx.types.Handle @llvm.dx.resource.casthandle.f32(target("dx.TypedBuffer", float, 1, 0, 1) poison)
  %h2 = call %dx.types.Handle @llvm.dx.resource.casthandle.v2f32(target("dx.TypedBuffer", <2 x float>, 1, 0, 1) poison)
  %h3 = call %dx.types.Handle @llvm.dx.resource.casthandle.v3f32(target("dx.TypedBuffer", <3 x float>, 1, 0, 1) poison)
  ; CHECK: call void @llvm.dx.resource.store.typedbuffer{{.*}}(target("dx.TypedBuffer", float, 1, 0, 1) poison, i32 0, <4 x float> {{%.*}})
  ; CHECK: call void @llvm.dx.resource.store.typedbuffer{{.*}}(target("dx.TypedBuffer", <2 x float>, 1, 0, 1) poison, i32 1, <4 x float> {{%.*}})
  ; CHECK: call void @llvm.dx.resource.store.typedbuffer{{.*}}(target("dx.TypedBuffer", <3 x float>, 1, 0, 1) poison, i32 2, <4 x float> {{%.*}})
  ; CHECK-NOT: call void @dx.op.bufferStore
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %h1, i32 0, i32 undef, float %x, float %y, float %z, float %w, i8 15)
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %h2, i32 1, i32 undef, float %x, float %y, float %z, float %w, i8 15)
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %h3, i32 2, i32 undef, float %x, float %y, float %z, float %w, i8 15)
  ret void
}

; Invalid masks and mismatched active operands must not construct partial IR.
; CHECK-LABEL: define void @reject_masks(
define void @reject_masks(i8 %mask, i32 %x) {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.i32(target("dx.TypedBuffer", i32, 1, 0, 0) poison)
  ; CHECK-NOT: insertelement
  ; CHECK-NOT: call void @llvm.dx.resource.store.typedbuffer
  ; CHECK: call void @dx.op.bufferStore.i32({{.*}}i8 %mask)
  ; CHECK: call void @dx.op.bufferStore.i32({{.*}}i8 0)
  ; CHECK: call void @dx.op.bufferStore.i32({{.*}}i8 5)
  ; CHECK: call void @dx.op.bufferStore.i32({{.*}}i8 31)
  ; CHECK: call void @dx.op.bufferStore.i32({{.*}}i8 3)
  ; CHECK-NOT: insertelement
  ; CHECK-NOT: call void @llvm.dx.resource.store.typedbuffer
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %h, i32 0, i32 undef, i32 %x, i32 %x, i32 %x, i32 %x, i8 %mask)
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %h, i32 0, i32 undef, i32 %x, i32 %x, i32 %x, i32 %x, i8 0)
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %h, i32 0, i32 undef, i32 %x, i32 %x, i32 %x, i32 %x, i8 5)
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %h, i32 0, i32 undef, i32 %x, i32 %x, i32 %x, i32 %x, i8 31)
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %h, i32 0, i32 undef, i32 %x, i32 %x, i32 %x, i32 %x, i8 3)
  ret void
}

; CHECK-LABEL: define void @reject_types(
define void @reject_types(float %x, i16 %h) {
  %hf = call %dx.types.Handle @llvm.dx.resource.casthandle.f32(target("dx.TypedBuffer", float, 1, 0, 1) poison)
  %hh = call %dx.types.Handle @llvm.dx.resource.casthandle.i16(target("dx.TypedBuffer", i16, 1, 0, 0) poison)
  ; CHECK-NOT: insertelement
  ; CHECK-NOT: call void @llvm.dx.resource.store.typedbuffer
  ; CHECK: call void @dx.op.bufferStore.mixed
  ; CHECK: call void @dx.op.bufferStore.i16
  ; CHECK-NOT: insertelement
  ; CHECK-NOT: call void @llvm.dx.resource.store.typedbuffer
  call void @dx.op.bufferStore.mixed(i32 69, %dx.types.Handle %hf, i32 0, i32 undef, float %x, float %x, i32 0, float %x, i8 15)
  call void @dx.op.bufferStore.i16(i32 69, %dx.types.Handle %hh, i32 0, i32 undef, i16 %h, i16 %h, i16 %h, i16 %h, i8 15)
  ret void
}

declare %dx.types.Handle @llvm.dx.resource.casthandle.i32(target("dx.TypedBuffer", i32, 1, 0, 0))
declare %dx.types.Handle @llvm.dx.resource.casthandle.f32(target("dx.TypedBuffer", float, 1, 0, 1))
declare %dx.types.Handle @llvm.dx.resource.casthandle.v2f32(target("dx.TypedBuffer", <2 x float>, 1, 0, 1))
declare %dx.types.Handle @llvm.dx.resource.casthandle.v3f32(target("dx.TypedBuffer", <3 x float>, 1, 0, 1))
declare %dx.types.Handle @llvm.dx.resource.casthandle.i16(target("dx.TypedBuffer", i16, 1, 0, 0))
declare void @dx.op.bufferStore.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i8)
declare void @dx.op.bufferStore.f32(i32, %dx.types.Handle, i32, i32, float, float, float, float, i8)
declare void @dx.op.bufferStore.mixed(i32, %dx.types.Handle, i32, i32, float, float, i32, float, i8)
declare void @dx.op.bufferStore.i16(i32, %dx.types.Handle, i32, i32, i16, i16, i16, i16, i8)
