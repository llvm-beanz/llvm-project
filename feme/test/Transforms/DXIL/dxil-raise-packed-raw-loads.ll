; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes='feme-dxil-raise-ops,verify,feme-dxil-raise-ops,verify' -S %s | FileCheck %s

target triple = "dxil-ms-dx"
%dx.types.Handle = type { ptr }
%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%dx.types.ResRet.i16 = type { i16, i16, i16, i16, i32 }
%dx.types.ResRet.i64 = type { i64, i64, i64, i64, i32 }
%dx.types.ResRet.mixed = type { i32, float, i32, i32, i32 }
%dx.types.ResRet.short = type { i32 }

; CHECK-LABEL: define <2 x i32> @modern_int2(
define <2 x i32> @modern_int2(i32 %offset) {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0) poison)
  ; CHECK: [[L:%.*]] = call { <2 x i32>, i1 } @llvm.dx.resource.load.rawbuffer{{.*}}(target("dx.RawBuffer", i8, 1, 0) poison, i32 %offset, i32 poison)
  ; CHECK: [[D:%.*]] = extractvalue { <2 x i32>, i1 } [[L]], 0
  ; CHECK: extractelement <2 x i32> [[D]], i64 0
  ; CHECK: extractelement <2 x i32> [[D]], i64 1
  ; CHECK-NOT: call {{.*}} @dx.op.rawBufferLoad
  %r = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %h, i32 %offset, i32 undef, i8 3, i32 4)
  %x = extractvalue %dx.types.ResRet.i32 %r, 0
  %y = extractvalue %dx.types.ResRet.i32 %r, 1
  %v = insertelement <2 x i32> poison, i32 %x, i32 0
  %v2 = insertelement <2 x i32> %v, i32 %y, i32 1
  ret <2 x i32> %v2
}

; An explicit modern mask retains its full width even for partial result uses.
; CHECK-LABEL: define i32 @modern_partial_result(
define i32 @modern_partial_result(i32 %offset) {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0) poison)
  ; CHECK: [[L:%.*]] = call { <4 x i32>, i1 } @llvm.dx.resource.load.rawbuffer
  ; CHECK: [[D:%.*]] = extractvalue { <4 x i32>, i1 } [[L]], 0
  ; CHECK: [[X:%.*]] = extractelement <4 x i32> [[D]], i64 0
  ; CHECK: ret i32 [[X]]
  %r = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %h, i32 %offset, i32 poison, i8 15, i32 4)
  %x = extractvalue %dx.types.ResRet.i32 %r, 0
  ret i32 %x
}

; Legacy width comes from the complete leading run, independent of use order.
; CHECK-LABEL: define <3 x float> @legacy_float3(
define <3 x float> @legacy_float3(i32 %index, i32 %offset) {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.structured(target("dx.RawBuffer", [16 x i8], 1, 0) poison)
  ; CHECK: [[L:%.*]] = call { <3 x float>, i1 } @llvm.dx.resource.load.rawbuffer{{.*}}(target("dx.RawBuffer", [16 x i8], 1, 0) poison, i32 %index, i32 %offset)
  ; CHECK: [[D:%.*]] = extractvalue { <3 x float>, i1 } [[L]], 0
  ; CHECK: extractelement <3 x float> [[D]], i64 2
  ; CHECK: extractelement <3 x float> [[D]], i64 0
  ; CHECK: extractelement <3 x float> [[D]], i64 1
  ; CHECK-NOT: call {{.*}} @dx.op.bufferLoad
  %r = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %h, i32 %index, i32 %offset)
  %z = extractvalue %dx.types.ResRet.f32 %r, 2
  %x = extractvalue %dx.types.ResRet.f32 %r, 0
  %y = extractvalue %dx.types.ResRet.f32 %r, 1
  %v = insertelement <3 x float> poison, float %x, i32 0
  %v2 = insertelement <3 x float> %v, float %y, i32 1
  %v3 = insertelement <3 x float> %v2, float %z, i32 2
  ret <3 x float> %v3
}

; CHECK-LABEL: define float @modern_float_widths(
define float @modern_float_widths() {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0) poison)
  ; CHECK: [[A:%.*]] = call { <2 x float>, i1 } @llvm.dx.resource.load.rawbuffer
  ; CHECK: [[DA:%.*]] = extractvalue { <2 x float>, i1 } [[A]], 0
  ; CHECK: [[B:%.*]] = call { <3 x float>, i1 } @llvm.dx.resource.load.rawbuffer
  ; CHECK: [[DB:%.*]] = extractvalue { <3 x float>, i1 } [[B]], 0
  ; CHECK: [[C:%.*]] = call { <4 x float>, i1 } @llvm.dx.resource.load.rawbuffer
  ; CHECK: [[DC:%.*]] = extractvalue { <4 x float>, i1 } [[C]], 0
  ; CHECK: extractelement <2 x float> [[DA]], i64 1
  ; CHECK: extractelement <2 x float> [[DA]], i64 0
  ; CHECK: extractelement <2 x float> [[DA]], i64 0
  ; CHECK: extractelement <3 x float> [[DB]], i64 2
  ; CHECK: extractelement <4 x float> [[DC]], i64 3
  ; CHECK-NOT: call {{.*}} @llvm.dx.resource.load.rawbuffer
  ; CHECK-NOT: call {{.*}} @dx.op.rawBufferLoad
  %a = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %h, i32 0, i32 undef, i8 3, i32 4)
  %b = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %h, i32 8, i32 undef, i8 7, i32 4)
  %c = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %h, i32 20, i32 undef, i8 15, i32 4)
  %ay = extractvalue %dx.types.ResRet.f32 %a, 1
  %ax = extractvalue %dx.types.ResRet.f32 %a, 0
  %ax2 = extractvalue %dx.types.ResRet.f32 %a, 0
  %bz = extractvalue %dx.types.ResRet.f32 %b, 2
  %cw = extractvalue %dx.types.ResRet.f32 %c, 3
  %s = fadd float %ay, %ax
  %s2 = fadd float %s, %ax2
  %s3 = fadd float %s2, %bz
  %s4 = fadd float %s3, %cw
  ret float %s4
}

; CHECK-LABEL: define float @legacy_float_widths(
define float @legacy_float_widths() {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0) poison)
  ; CHECK: call { <2 x float>, i1 } @llvm.dx.resource.load.rawbuffer
  ; CHECK: call { <4 x float>, i1 } @llvm.dx.resource.load.rawbuffer
  ; CHECK-NOT: call {{.*}} @dx.op.bufferLoad
  %a = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %h, i32 0, i32 undef)
  %b = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %h, i32 8, i32 undef)
  %ax = extractvalue %dx.types.ResRet.f32 %a, 0
  %ay = extractvalue %dx.types.ResRet.f32 %a, 1
  %bx = extractvalue %dx.types.ResRet.f32 %b, 0
  %by = extractvalue %dx.types.ResRet.f32 %b, 1
  %bz = extractvalue %dx.types.ResRet.f32 %b, 2
  %bw = extractvalue %dx.types.ResRet.f32 %b, 3
  %s = fadd float %ax, %ay
  %s2 = fadd float %s, %bx
  %s3 = fadd float %s2, %by
  %s4 = fadd float %s3, %bz
  %s5 = fadd float %s4, %bw
  ret float %s5
}

; CHECK-LABEL: define <2 x i32> @reject_legacy_gap(
define <2 x i32> @reject_legacy_gap(i32 %offset) {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0) poison)
  ; CHECK-NOT: call {{.*}} @llvm.dx.resource.load.rawbuffer
  ; CHECK: [[L:%.*]] = call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32
  ; CHECK: extractvalue %dx.types.ResRet.i32 [[L]], 0
  ; CHECK: extractvalue %dx.types.ResRet.i32 [[L]], 2
  ; CHECK-NOT: call {{.*}} @llvm.dx.resource.load.rawbuffer
  %r = call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32 68, %dx.types.Handle %h, i32 %offset, i32 undef)
  %x = extractvalue %dx.types.ResRet.i32 %r, 0
  %z = extractvalue %dx.types.ResRet.i32 %r, 2
  %v = insertelement <2 x i32> poison, i32 %x, i32 0
  %v2 = insertelement <2 x i32> %v, i32 %z, i32 1
  ret <2 x i32> %v2
}

; CHECK-LABEL: define i32 @reject_masks(
define i32 @reject_masks(i8 %mask) {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0) poison)
  ; CHECK-NOT: call {{.*}} @llvm.dx.resource.load.rawbuffer
  ; CHECK: call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32({{.*}}i8 %mask, i32 4)
  ; CHECK: call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32({{.*}}i8 0, i32 4)
  ; CHECK: call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32({{.*}}i8 5, i32 4)
  ; CHECK: call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32({{.*}}i8 31, i32 4)
  ; CHECK: call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32({{.*}}i8 3, i32 4)
  ; CHECK-NOT: call {{.*}} @llvm.dx.resource.load.rawbuffer
  %a = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %h, i32 0, i32 undef, i8 %mask, i32 4)
  %b = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %h, i32 0, i32 undef, i8 0, i32 4)
  %c = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %h, i32 0, i32 undef, i8 5, i32 4)
  %d = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %h, i32 0, i32 undef, i8 31, i32 4)
  %e = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %h, i32 0, i32 undef, i8 3, i32 4)
  %ax = extractvalue %dx.types.ResRet.i32 %a, 0
  %bx = extractvalue %dx.types.ResRet.i32 %b, 0
  %cx = extractvalue %dx.types.ResRet.i32 %c, 0
  %dx = extractvalue %dx.types.ResRet.i32 %d, 0
  %ex = extractvalue %dx.types.ResRet.i32 %e, 2
  %ab = add i32 %ax, %bx
  %cd = add i32 %cx, %dx
  %abcd = add i32 %ab, %cd
  %all = add i32 %abcd, %ex
  ret i32 %all
}

; CHECK-LABEL: define i32 @reject_result_types(
define i32 @reject_result_types() {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0) poison)
  ; CHECK-NOT: call {{.*}} @llvm.dx.resource.load.rawbuffer
  ; CHECK: call %dx.types.ResRet.i16 @dx.op.rawBufferLoad.i16
  ; CHECK: call %dx.types.ResRet.i64 @dx.op.rawBufferLoad.i64
  ; CHECK: call %dx.types.ResRet.mixed @dx.op.rawBufferLoad.mixed
  ; CHECK: call %dx.types.ResRet.short @dx.op.rawBufferLoad.short
  ; CHECK-NOT: call {{.*}} @llvm.dx.resource.load.rawbuffer
  %a = call %dx.types.ResRet.i16 @dx.op.rawBufferLoad.i16(i32 139, %dx.types.Handle %h, i32 0, i32 poison, i8 3, i32 2)
  %wide = call %dx.types.ResRet.i64 @dx.op.rawBufferLoad.i64(i32 139, %dx.types.Handle %h, i32 0, i32 poison, i8 3, i32 8)
  %b = call %dx.types.ResRet.mixed @dx.op.rawBufferLoad.mixed(i32 139, %dx.types.Handle %h, i32 0, i32 poison, i8 3, i32 4)
  %c = call %dx.types.ResRet.short @dx.op.rawBufferLoad.short(i32 139, %dx.types.Handle %h, i32 0, i32 poison, i8 3, i32 4)
  %x = extractvalue %dx.types.ResRet.i16 %a, 0
  %w = extractvalue %dx.types.ResRet.i64 %wide, 0
  %y = extractvalue %dx.types.ResRet.mixed %b, 0
  %z = extractvalue %dx.types.ResRet.short %c, 0
  %xi = zext i16 %x to i32
  %xy = add i32 %xi, %y
  %xyz = add i32 %xy, %z
  %wi = trunc i64 %w to i32
  %all = add i32 %xyz, %wi
  ret i32 %all
}

; Status use must not create a duplicate read even when data is also used.
; CHECK-LABEL: define i32 @reject_status(
define i32 @reject_status() {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0) poison)
  ; CHECK-NOT: call {{.*}} @llvm.dx.resource.load.rawbuffer
  ; CHECK: [[L:%.*]] = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32
  ; CHECK: extractvalue %dx.types.ResRet.i32 [[L]], 0
  ; CHECK: extractvalue %dx.types.ResRet.i32 [[L]], 4
  ; CHECK-NOT: call {{.*}} @llvm.dx.resource.load.rawbuffer
  %r = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %h, i32 0, i32 poison, i8 15, i32 4)
  %x = extractvalue %dx.types.ResRet.i32 %r, 0
  %s = extractvalue %dx.types.ResRet.i32 %r, 4
  %v = add i32 %x, %s
  ret i32 %v
}

; Scalar type support is not narrowed by packed admission.
; CHECK-LABEL: define i64 @scalar_i64(
define i64 @scalar_i64() {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0) poison)
  ; CHECK: [[L:%.*]] = call { i64, i1 } @llvm.dx.resource.load.rawbuffer
  ; CHECK: [[D:%.*]] = extractvalue { i64, i1 } [[L]], 0
  ; CHECK: ret i64 [[D]]
  %r = call %dx.types.ResRet.i64 @dx.op.rawBufferLoad.i64(i32 139, %dx.types.Handle %h, i32 0, i32 undef, i8 1, i32 8)
  %x = extractvalue %dx.types.ResRet.i64 %r, 0
  ret i64 %x
}

; CHECK-LABEL: define void @reject_unused(
define void @reject_unused() {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0) poison)
  ; CHECK-NOT: call {{.*}} @llvm.dx.resource.load.rawbuffer
  ; CHECK: call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32
  ; CHECK-NOT: call {{.*}} @llvm.dx.resource.load.rawbuffer
  ; CHECK: ret void
  %r = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %h, i32 0, i32 undef, i8 15, i32 4)
  ret void
}

; CHECK-LABEL: define %dx.types.ResRet.i32 @reject_aggregate(
define %dx.types.ResRet.i32 @reject_aggregate() {
  %h = call %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0) poison)
  ; CHECK-NOT: call {{.*}} @llvm.dx.resource.load.rawbuffer
  ; CHECK: [[L:%.*]] = call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32
  ; CHECK: ret %dx.types.ResRet.i32 [[L]]
  %r = call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32 68, %dx.types.Handle %h, i32 0, i32 undef)
  ret %dx.types.ResRet.i32 %r
}

declare %dx.types.Handle @llvm.dx.resource.casthandle.raw(target("dx.RawBuffer", i8, 1, 0))
declare %dx.types.Handle @llvm.dx.resource.casthandle.structured(target("dx.RawBuffer", [16 x i8], 1, 0))
declare %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32, %dx.types.Handle, i32, i32, i8, i32)
declare %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32, %dx.types.Handle, i32, i32)
declare %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32, %dx.types.Handle, i32, i32)
declare %dx.types.ResRet.i16 @dx.op.rawBufferLoad.i16(i32, %dx.types.Handle, i32, i32, i8, i32)
declare %dx.types.ResRet.i64 @dx.op.rawBufferLoad.i64(i32, %dx.types.Handle, i32, i32, i8, i32)
declare %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32, %dx.types.Handle, i32, i32, i8, i32)
declare %dx.types.ResRet.mixed @dx.op.rawBufferLoad.mixed(i32, %dx.types.Handle, i32, i32, i8, i32)
declare %dx.types.ResRet.short @dx.op.rawBufferLoad.short(i32, %dx.types.Handle, i32, i32, i8, i32)
