; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes='feme-dxil-raise-ops,verify,feme-dxil-raise-ops,verify' -S %s | FileCheck %s
;
; Packed raw/structured stores retain one vector write, not independent
; scalar writes that could partially commit an out-of-bounds access.

target triple = "dxil-ms-dx"
%dx.types.Handle = type { ptr }

; CHECK-LABEL: define void @raw_float2(
define void @raw_float2(float %x, float %y, i32 %offset) {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0) poison)
  ; CHECK: [[V:%.*]] = insertelement <2 x float> poison, float %x, i32 0
  ; CHECK: [[V2:%.*]] = insertelement <2 x float> [[V]], float %y, i32 1
  ; CHECK: call void @llvm.dx.resource.store.rawbuffer{{.*}}(target("dx.RawBuffer", i8, 1, 0) poison, i32 %offset, i32 poison, <2 x float> [[V2]])
  ; CHECK-NOT: call void @dx.op
  call void @dx.op.rawBufferStore.f32(i32 140, %dx.types.Handle %h, i32 %offset, i32 undef, float %x, float %y, float poison, float poison, i8 3, i32 4)
  ret void
}

; CHECK-LABEL: define void @structured_float3(
define void @structured_float3(float %x, float %y, float %z, i32 %index, i32 %offset) {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.structured(target("dx.RawBuffer", [16 x i8], 1, 0) poison)
  ; CHECK: [[V:%.*]] = insertelement <3 x float> poison, float %x, i32 0
  ; CHECK: [[V2:%.*]] = insertelement <3 x float> [[V]], float %y, i32 1
  ; CHECK: [[V3:%.*]] = insertelement <3 x float> [[V2]], float %z, i32 2
  ; CHECK: call void @llvm.dx.resource.store.rawbuffer{{.*}}(target("dx.RawBuffer", [16 x i8], 1, 0) poison, i32 %index, i32 %offset, <3 x float> [[V3]])
  ; CHECK-NOT: call void @dx.op
  call void @dx.op.rawBufferStore.f32(i32 140, %dx.types.Handle %h, i32 %index, i32 %offset, float %x, float %y, float %z, float poison, i8 7, i32 4)
  ret void
}

; CHECK-LABEL: define void @raw_int4(
define void @raw_int4(i32 %x, i32 %y, i32 %z, i32 %w, i32 %offset) {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0) poison)
  ; CHECK: [[V:%.*]] = insertelement <4 x i32> poison, i32 %x, i32 0
  ; CHECK: [[V2:%.*]] = insertelement <4 x i32> [[V]], i32 %y, i32 1
  ; CHECK: [[V3:%.*]] = insertelement <4 x i32> [[V2]], i32 %z, i32 2
  ; CHECK: [[V4:%.*]] = insertelement <4 x i32> [[V3]], i32 %w, i32 3
  ; CHECK: call void @llvm.dx.resource.store.rawbuffer{{.*}}(target("dx.RawBuffer", i8, 1, 0) poison, i32 %offset, i32 poison, <4 x i32> [[V4]])
  ; CHECK-NOT: call void @dx.op
  call void @dx.op.rawBufferStore.i32(i32 140, %dx.types.Handle %h, i32 %offset, i32 poison, i32 %x, i32 %y, i32 %z, i32 %w, i8 15, i32 4)
  ret void
}

; CHECK-LABEL: define void @legacy_float4(
define void @legacy_float4(float %x, float %y, float %z, float %w, i32 %offset) {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0) poison)
  ; CHECK: [[V:%.*]] = insertelement <4 x float> poison, float %x, i32 0
  ; CHECK: [[V2:%.*]] = insertelement <4 x float> [[V]], float %y, i32 1
  ; CHECK: [[V3:%.*]] = insertelement <4 x float> [[V2]], float %z, i32 2
  ; CHECK: [[V4:%.*]] = insertelement <4 x float> [[V3]], float %w, i32 3
  ; CHECK: call void @llvm.dx.resource.store.rawbuffer{{.*}}(target("dx.RawBuffer", i8, 1, 0) poison, i32 %offset, i32 poison, <4 x float> [[V4]])
  ; CHECK-NOT: call void @dx.op
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %h, i32 %offset, i32 undef, float %x, float %y, float %z, float %w, i8 15)
  ret void
}

; CHECK-LABEL: define void @reject_masks(
define void @reject_masks(i8 %mask) {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0) poison)
  ; CHECK-NOT: call void @llvm.dx.resource.store.rawbuffer
  ; CHECK: call void @dx.op.rawBufferStore.i32({{.*}}i8 %mask, i32 4)
  ; CHECK: call void @dx.op.rawBufferStore.i32({{.*}}i8 0, i32 4)
  ; CHECK: call void @dx.op.rawBufferStore.i32({{.*}}i8 5, i32 4)
  ; CHECK: call void @dx.op.rawBufferStore.i32({{.*}}i8 31, i32 4)
  ; CHECK: call void @dx.op.bufferStore.f32({{.*}}i8 6)
  ; CHECK-NOT: call void @llvm.dx.resource.store.rawbuffer
  call void @dx.op.rawBufferStore.i32(i32 140, %dx.types.Handle %h, i32 0, i32 undef, i32 1, i32 2, i32 3, i32 4, i8 %mask, i32 4)
  call void @dx.op.rawBufferStore.i32(i32 140, %dx.types.Handle %h, i32 0, i32 undef, i32 1, i32 2, i32 3, i32 4, i8 0, i32 4)
  call void @dx.op.rawBufferStore.i32(i32 140, %dx.types.Handle %h, i32 0, i32 undef, i32 1, i32 2, i32 3, i32 4, i8 5, i32 4)
  call void @dx.op.rawBufferStore.i32(i32 140, %dx.types.Handle %h, i32 0, i32 undef, i32 1, i32 2, i32 3, i32 4, i8 31, i32 4)
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %h, i32 0, i32 undef, float 1.0, float 2.0, float 3.0, float 4.0, i8 6)
  ret void
}

; CHECK-LABEL: define void @reject_types(
define void @reject_types() {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0) poison)
  ; CHECK-NOT: insertelement
  ; CHECK-NOT: call void @llvm.dx.resource.store.rawbuffer
  ; CHECK: call void @dx.op.rawBufferStore.i16(
  ; CHECK: call void @dx.op.rawBufferStore.i64(
  ; CHECK: call void @dx.op.rawBufferStore.mixed(
  ; CHECK-NOT: insertelement
  ; CHECK-NOT: call void @llvm.dx.resource.store.rawbuffer
  call void @dx.op.rawBufferStore.i16(i32 140, %dx.types.Handle %h, i32 0, i32 undef, i16 1, i16 2, i16 poison, i16 poison, i8 3, i32 2)
  call void @dx.op.rawBufferStore.i64(i32 140, %dx.types.Handle %h, i32 0, i32 undef, i64 1, i64 2, i64 poison, i64 poison, i8 3, i32 8)
  call void @dx.op.rawBufferStore.mixed(i32 140, %dx.types.Handle %h, i32 0, i32 undef, i32 1, float 2.0, i32 poison, i32 poison, i8 3, i32 4)
  ret void
}

declare %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0))
declare %dx.types.Handle @llvm.dx.resource.casthandle.structured(target("dx.RawBuffer", [16 x i8], 1, 0))
declare void @dx.op.rawBufferStore.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i8, i32)
declare void @dx.op.rawBufferStore.f32(i32, %dx.types.Handle, i32, i32, float, float, float, float, i8, i32)
declare void @dx.op.bufferStore.f32(i32, %dx.types.Handle, i32, i32, float, float, float, float, i8)
declare void @dx.op.rawBufferStore.i16(i32, %dx.types.Handle, i32, i32, i16, i16, i16, i16, i8, i32)
declare void @dx.op.rawBufferStore.i64(i32, %dx.types.Handle, i32, i32, i64, i64, i64, i64, i8, i32)
declare void @dx.op.rawBufferStore.mixed(i32, %dx.types.Handle, i32, i32, i32, float, i32, i32, i8, i32)
