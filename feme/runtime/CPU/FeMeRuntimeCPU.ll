;===- FeMeRuntimeCPU.ll - Resource access scalar helper IR --------------===;
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM
; Exceptions. See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
;===----------------------------------------------------------------------===;
;
; This is `libFeMeRuntimeCPU`'s shader-side half (see "Runtime Support
; Library" in feme/docs/FeMeCPUDesign.md): the scalar implementations of the
; canonical `feme.cpu.resource.*` calls `feme::cpu::ResourceCalls`/
; `feme::cpu::ResourceLoweringPass` create. A later phase (widening, the
; entry wrapper) links only the referenced definitions from this file into a
; compiled shader module and internalizes them, so the ordinary optimizer
; can inline and constant-fold through them before codegen -- see
; "Descriptor formats"/"Bounds checking" in feme/docs/FeMeCPUDesign.md for
; why that has to happen before optimization rather than at link time
; against a separately-compiled library.
;
; The descriptor layout (`%feme.cpu.rt.Descriptor`) mirrors
; `feme::cpu::FemeDescriptor` in
; feme/include/feme/Target/CPU/RuntimeABI.h field for field; keep the two in
; sync. `ResourceKind`/`ResourceFormat`/`FemeDescriptorFlagBits`'s numeric
; values are hardcoded as integer literals below (LLVM IR has no #include),
; and are likewise called out at each use so a future enumerator renumbering
; in RuntimeABI.h is easy to find and fix here.
;
; Scope (roadmap milestone 3): this file covers the typed-buffer view
; `<4 x float>` (covering the `R32G32B32A32_FLOAT` identity format and the
; packed `R8G8B8A8_UNORM` format, to establish the format-switch pattern
; concretely) and the raw/structured-buffer views `i32`/`float`. Every
; other canonical call `feme::cpu::ResourceCalls` can create (other typed
; views, the remaining formats "Descriptor formats" lists, atomics) is a
; mechanical repeat of the same pattern once a call site actually needs it
; -- "Additional formats extend one helper implementation rather than every
; access site" -- and is added on demand rather than spelled out
; exhaustively up front.
;
;===----------------------------------------------------------------------===;

; Mirrors `feme::cpu::FemeDescriptor` (RuntimeABI.h): { Data, SizeInBytes,
; Stride, Format, Kind, Flags, Counter }.
%feme.cpu.rt.Descriptor = type { ptr, i64, i32, i32, i32, i32, ptr }

; An internal, already-bounds-checked view of one descriptor's fields, plus
; whether the heap index itself was in range (`IndexOK`) -- the "always on"
; check from "Bounds checking" -- returned instead of the raw struct so
; every caller shares one heap-index-check implementation.
%feme.cpu.rt.Loaded = type { ptr, i64, i32, i32, i32, i32, i1 }

;--- Shared helpers ----------------------------------------------------------

; Loads descriptor `%index` of `%heap`/`%heap_count`, or an all-zero
; (`Kind = None`) `%feme.cpu.rt.Loaded` with `IndexOK = false` if
; `%index >= %heap_count` -- "An index >= HeapCount yields the all-zero
; descriptor. Never skippable" (see "Per-descriptor control"). This never
; reads through `%heap` at all when the index is out of range, since
; `%heap_count == 0` means `%heap` may not point at anything.
define internal %feme.cpu.rt.Loaded @feme.cpu.rt.load_descriptor(
    ptr %heap, i32 %heap_count, i32 %index) alwaysinline {
entry:
  %index_ok = icmp ult i32 %index, %heap_count
  br i1 %index_ok, label %in_bounds, label %out_of_bounds

in_bounds:
  %index64 = zext i32 %index to i64
  %desc_ptr = getelementptr %feme.cpu.rt.Descriptor, ptr %heap, i64 %index64
  %data_ptr = getelementptr %feme.cpu.rt.Descriptor, ptr %desc_ptr, i32 0, i32 0
  %data_in = load ptr, ptr %data_ptr, align 8
  %size_ptr = getelementptr %feme.cpu.rt.Descriptor, ptr %desc_ptr, i32 0, i32 1
  %size_in = load i64, ptr %size_ptr, align 8
  %stride_ptr = getelementptr %feme.cpu.rt.Descriptor, ptr %desc_ptr, i32 0, i32 2
  %stride_in = load i32, ptr %stride_ptr, align 4
  %format_ptr = getelementptr %feme.cpu.rt.Descriptor, ptr %desc_ptr, i32 0, i32 3
  %format_in = load i32, ptr %format_ptr, align 4
  %kind_ptr = getelementptr %feme.cpu.rt.Descriptor, ptr %desc_ptr, i32 0, i32 4
  %kind_in = load i32, ptr %kind_ptr, align 4
  %flags_ptr = getelementptr %feme.cpu.rt.Descriptor, ptr %desc_ptr, i32 0, i32 5
  %flags_in = load i32, ptr %flags_ptr, align 4
  br label %join

out_of_bounds:
  br label %join

join:
  %data = phi ptr [ %data_in, %in_bounds ], [ null, %out_of_bounds ]
  %size = phi i64 [ %size_in, %in_bounds ], [ 0, %out_of_bounds ]
  %stride = phi i32 [ %stride_in, %in_bounds ], [ 0, %out_of_bounds ]
  %format = phi i32 [ %format_in, %in_bounds ], [ 0, %out_of_bounds ]
  ; ResourceKind::None == 0 (RuntimeABI.h): the out-of-bounds default.
  %kind = phi i32 [ %kind_in, %in_bounds ], [ 0, %out_of_bounds ]
  %flags = phi i32 [ %flags_in, %in_bounds ], [ 0, %out_of_bounds ]
  %r0 = insertvalue %feme.cpu.rt.Loaded poison, ptr %data, 0
  %r1 = insertvalue %feme.cpu.rt.Loaded %r0, i64 %size, 1
  %r2 = insertvalue %feme.cpu.rt.Loaded %r1, i32 %stride, 2
  %r3 = insertvalue %feme.cpu.rt.Loaded %r2, i32 %format, 3
  %r4 = insertvalue %feme.cpu.rt.Loaded %r3, i32 %kind, 4
  %r5 = insertvalue %feme.cpu.rt.Loaded %r4, i32 %flags, 5
  %r6 = insertvalue %feme.cpu.rt.Loaded %r5, i1 %index_ok, 6
  ret %feme.cpu.rt.Loaded %r6
}

; Whether an access of `%access_size` bytes at `%offset` through a
; descriptor of kind `%kind`/size `%size`/flags `%flags` is allowed: the
; descriptor's kind must match `%expected_kind` (which is never
; `ResourceKind::None == 0`, so this also implements "ignored when
; `Kind == None`" -- an all-zero descriptor never matches), and the access
; must either fit (`Offset + AccessSize <= SizeInBytes`, see "Bounds
; checking") or the descriptor must carry
; `FEME_DESCRIPTOR_TRUSTED == 1 << 3` (see "Per-descriptor control").
define internal i1 @feme.cpu.rt.check_access(
    i32 %kind, i32 %expected_kind, i64 %size, i32 %flags, i64 %offset,
    i64 %access_size) alwaysinline {
  %kind_ok = icmp eq i32 %kind, %expected_kind
  %end = add i64 %offset, %access_size
  %range_ok = icmp ule i64 %end, %size
  %trusted_bit = and i32 %flags, 8
  %trusted = icmp ne i32 %trusted_bit, 0
  %offset_ok = or i1 %range_ok, %trusted
  %ok = and i1 %kind_ok, %offset_ok
  ret i1 %ok
}

; Unpacks a `R8G8B8A8_UNORM` value (four normalized `[0, 255]` bytes,
; little-endian: R, G, B, A) into a `<4 x float>` in `[0.0, 1.0]`.
define internal <4 x float> @feme.cpu.rt.unpack_r8g8b8a8_unorm(
    i32 %raw) alwaysinline {
  %b0 = trunc i32 %raw to i8
  %s1 = lshr i32 %raw, 8
  %b1 = trunc i32 %s1 to i8
  %s2 = lshr i32 %raw, 16
  %b2 = trunc i32 %s2 to i8
  %s3 = lshr i32 %raw, 24
  %b3 = trunc i32 %s3 to i8
  %u0 = uitofp i8 %b0 to float
  %u1 = uitofp i8 %b1 to float
  %u2 = uitofp i8 %b2 to float
  %u3 = uitofp i8 %b3 to float
  %f0 = fdiv float %u0, 255.0
  %f1 = fdiv float %u1, 255.0
  %f2 = fdiv float %u2, 255.0
  %f3 = fdiv float %u3, 255.0
  %v0 = insertelement <4 x float> poison, float %f0, i32 0
  %v1 = insertelement <4 x float> %v0, float %f1, i32 1
  %v2 = insertelement <4 x float> %v1, float %f2, i32 2
  %v3 = insertelement <4 x float> %v2, float %f3, i32 3
  ret <4 x float> %v3
}

; The inverse of `feme.cpu.rt.unpack_r8g8b8a8_unorm`: clamps each component
; to `[0.0, 1.0]`, scales to `[0, 255]`, and packs the four rounded bytes
; little-endian into one `i32`.
define internal i32 @feme.cpu.rt.pack_r8g8b8a8_unorm(
    <4 x float> %value) alwaysinline {
  %e0 = extractelement <4 x float> %value, i32 0
  %e1 = extractelement <4 x float> %value, i32 1
  %e2 = extractelement <4 x float> %value, i32 2
  %e3 = extractelement <4 x float> %value, i32 3
  %c0 = call float @llvm.maxnum.f32(float %e0, float 0.0)
  %c0c = call float @llvm.minnum.f32(float %c0, float 1.0)
  %c1 = call float @llvm.maxnum.f32(float %e1, float 0.0)
  %c1c = call float @llvm.minnum.f32(float %c1, float 1.0)
  %c2 = call float @llvm.maxnum.f32(float %e2, float 0.0)
  %c2c = call float @llvm.minnum.f32(float %c2, float 1.0)
  %c3 = call float @llvm.maxnum.f32(float %e3, float 0.0)
  %c3c = call float @llvm.minnum.f32(float %c3, float 1.0)
  %s0 = fmul float %c0c, 255.0
  %s1 = fmul float %c1c, 255.0
  %s2 = fmul float %c2c, 255.0
  %s3 = fmul float %c3c, 255.0
  %r0 = call float @llvm.round.f32(float %s0)
  %r1 = call float @llvm.round.f32(float %s1)
  %r2 = call float @llvm.round.f32(float %s2)
  %r3 = call float @llvm.round.f32(float %s3)
  %i0 = fptoui float %r0 to i8
  %i1 = fptoui float %r1 to i8
  %i2 = fptoui float %r2 to i8
  %i3 = fptoui float %r3 to i8
  %z0 = zext i8 %i0 to i32
  %z1 = zext i8 %i1 to i32
  %z2 = zext i8 %i2 to i32
  %z3 = zext i8 %i3 to i32
  %sh1 = shl i32 %z1, 8
  %sh2 = shl i32 %z2, 16
  %sh3 = shl i32 %z3, 24
  %p1 = or i32 %z0, %sh1
  %p2 = or i32 %p1, %sh2
  %p3 = or i32 %p2, %sh3
  ret i32 %p3
}

;--- Typed-buffer `<4 x float>` view ------------------------------------------

; `feme.cpu.resource.load.typed.v4f32` (see `feme::cpu::ResourceCalls`):
; reads a `<4 x float>` element through a bindless typed-buffer descriptor,
; switching on `Format` between the `R32G32B32A32_FLOAT` identity format
; (`== 4`) and the packed `R8G8B8A8_UNORM` format (`== 13`) -- see
; `feme::cpu::ResourceFormat` in RuntimeABI.h for those numeric values, and
; "Descriptor formats" for why the conversion has to be a runtime switch
; rather than something the compiler can select at compile time.
; `ResourceKind::Typed == 1`. An inactive lane (`%mask == false`) or a
; failing bounds/kind check reads as zero, never touching `%heap`'s memory
; (see "Bounds checking").
define <4 x float> @feme.cpu.resource.load.typed.v4f32(
    ptr %heap, i32 %heap_count, i32 %descriptor_index, i64 %element_index,
    i1 %mask) alwaysinline {
entry:
  %desc = call %feme.cpu.rt.Loaded @feme.cpu.rt.load_descriptor(
      ptr %heap, i32 %heap_count, i32 %descriptor_index)
  %data = extractvalue %feme.cpu.rt.Loaded %desc, 0
  %size = extractvalue %feme.cpu.rt.Loaded %desc, 1
  %format = extractvalue %feme.cpu.rt.Loaded %desc, 3
  %kind = extractvalue %feme.cpu.rt.Loaded %desc, 4
  %flags = extractvalue %feme.cpu.rt.Loaded %desc, 5
  %is_packed = icmp eq i32 %format, 13
  %elem_size = select i1 %is_packed, i64 4, i64 16
  %byte_offset = mul i64 %element_index, %elem_size
  %access_ok = call i1 @feme.cpu.rt.check_access(
      i32 %kind, i32 1, i64 %size, i32 %flags, i64 %byte_offset, i64 %elem_size)
  %do_access = and i1 %access_ok, %mask
  br i1 %do_access, label %access, label %zero

access:
  %ptr = getelementptr i8, ptr %data, i64 %byte_offset
  br i1 %is_packed, label %packed, label %identity

identity:
  %v_identity = load <4 x float>, ptr %ptr, align 4
  br label %join

packed:
  %raw = load i32, ptr %ptr, align 4
  %v_packed = call <4 x float> @feme.cpu.rt.unpack_r8g8b8a8_unorm(i32 %raw)
  br label %join

join:
  %v = phi <4 x float> [ %v_identity, %identity ], [ %v_packed, %packed ]
  br label %ret

zero:
  br label %ret

ret:
  %result = phi <4 x float> [ %v, %join ], [ zeroinitializer, %zero ]
  ret <4 x float> %result
}

; `feme.cpu.resource.store.typed.v4f32`: the store counterpart of
; `feme.cpu.resource.load.typed.v4f32` above -- same descriptor lookup,
; bounds/kind check and format switch, plus the UAV check "Descriptor
; heaps" requires for any write (`FEME_DESCRIPTOR_UAV == 1 << 0`; a
; constant buffer or other read-only view's `Flags` never sets it, so a
; store through one is silently dropped rather than corrupting it). An
; out-of-bounds or inactive-lane write is dropped, never touching
; `%heap`'s memory, matching the load's "reads zero" rule with "writes
; ignored" (see "Bounds checking").
define void @feme.cpu.resource.store.typed.v4f32(
    ptr %heap, i32 %heap_count, i32 %descriptor_index, i64 %element_index,
    <4 x float> %value, i1 %mask) alwaysinline {
entry:
  %desc = call %feme.cpu.rt.Loaded @feme.cpu.rt.load_descriptor(
      ptr %heap, i32 %heap_count, i32 %descriptor_index)
  %data = extractvalue %feme.cpu.rt.Loaded %desc, 0
  %size = extractvalue %feme.cpu.rt.Loaded %desc, 1
  %format = extractvalue %feme.cpu.rt.Loaded %desc, 3
  %kind = extractvalue %feme.cpu.rt.Loaded %desc, 4
  %flags = extractvalue %feme.cpu.rt.Loaded %desc, 5
  %is_packed = icmp eq i32 %format, 13
  %elem_size = select i1 %is_packed, i64 4, i64 16
  %byte_offset = mul i64 %element_index, %elem_size
  %access_ok = call i1 @feme.cpu.rt.check_access(
      i32 %kind, i32 1, i64 %size, i32 %flags, i64 %byte_offset, i64 %elem_size)
  %uav_bit = and i32 %flags, 1
  %is_uav = icmp ne i32 %uav_bit, 0
  %do_access0 = and i1 %access_ok, %mask
  %do_access = and i1 %do_access0, %is_uav
  br i1 %do_access, label %access, label %skip

access:
  %ptr = getelementptr i8, ptr %data, i64 %byte_offset
  br i1 %is_packed, label %packed, label %identity

identity:
  store <4 x float> %value, ptr %ptr, align 4
  ret void

packed:
  %raw = call i32 @feme.cpu.rt.pack_r8g8b8a8_unorm(<4 x float> %value)
  store i32 %raw, ptr %ptr, align 4
  ret void

skip:
  ret void
}

;--- Raw/structured-buffer views ----------------------------------------------

; `feme.cpu.resource.load.raw.i32`/`.f32`: read a scalar through a bindless
; raw or structured buffer descriptor at a byte offset `feme::cpu::
; ResourceLoweringPass` already computed (see "Descriptor heaps": no format
; conversion applies to raw/structured views, only the bounds/kind check).
; Both `ResourceKind::Raw == 3` and `ResourceKind::Structured == 2` are
; accepted, since the two share this call family (an unstructured
; `ByteAddressBuffer`'s descriptor is `Kind::Raw`; a `StructuredBuffer`'s is
; `Kind::Structured`).
define i32 @feme.cpu.resource.load.raw.i32(
    ptr %heap, i32 %heap_count, i32 %descriptor_index, i64 %byte_offset,
    i1 %mask) alwaysinline {
entry:
  %desc = call %feme.cpu.rt.Loaded @feme.cpu.rt.load_descriptor(
      ptr %heap, i32 %heap_count, i32 %descriptor_index)
  %data = extractvalue %feme.cpu.rt.Loaded %desc, 0
  %size = extractvalue %feme.cpu.rt.Loaded %desc, 1
  %kind = extractvalue %feme.cpu.rt.Loaded %desc, 4
  %flags = extractvalue %feme.cpu.rt.Loaded %desc, 5
  %ok_raw = call i1 @feme.cpu.rt.check_access(
      i32 %kind, i32 3, i64 %size, i32 %flags, i64 %byte_offset, i64 4)
  %ok_structured = call i1 @feme.cpu.rt.check_access(
      i32 %kind, i32 2, i64 %size, i32 %flags, i64 %byte_offset, i64 4)
  %ok_kind = or i1 %ok_raw, %ok_structured
  %do_access = and i1 %ok_kind, %mask
  br i1 %do_access, label %access, label %zero

access:
  %ptr = getelementptr i8, ptr %data, i64 %byte_offset
  %v = load i32, ptr %ptr, align 4
  ret i32 %v

zero:
  ret i32 0
}

define void @feme.cpu.resource.store.raw.i32(
    ptr %heap, i32 %heap_count, i32 %descriptor_index, i64 %byte_offset,
    i32 %value, i1 %mask) alwaysinline {
entry:
  %desc = call %feme.cpu.rt.Loaded @feme.cpu.rt.load_descriptor(
      ptr %heap, i32 %heap_count, i32 %descriptor_index)
  %data = extractvalue %feme.cpu.rt.Loaded %desc, 0
  %size = extractvalue %feme.cpu.rt.Loaded %desc, 1
  %kind = extractvalue %feme.cpu.rt.Loaded %desc, 4
  %flags = extractvalue %feme.cpu.rt.Loaded %desc, 5
  %ok_raw = call i1 @feme.cpu.rt.check_access(
      i32 %kind, i32 3, i64 %size, i32 %flags, i64 %byte_offset, i64 4)
  %ok_structured = call i1 @feme.cpu.rt.check_access(
      i32 %kind, i32 2, i64 %size, i32 %flags, i64 %byte_offset, i64 4)
  %ok_kind = or i1 %ok_raw, %ok_structured
  %uav_bit = and i32 %flags, 1
  %is_uav = icmp ne i32 %uav_bit, 0
  %do_access0 = and i1 %ok_kind, %mask
  %do_access = and i1 %do_access0, %is_uav
  br i1 %do_access, label %access, label %skip

access:
  %ptr = getelementptr i8, ptr %data, i64 %byte_offset
  store i32 %value, ptr %ptr, align 4
  ret void

skip:
  ret void
}

define float @feme.cpu.resource.load.raw.f32(
    ptr %heap, i32 %heap_count, i32 %descriptor_index, i64 %byte_offset,
    i1 %mask) alwaysinline {
entry:
  %desc = call %feme.cpu.rt.Loaded @feme.cpu.rt.load_descriptor(
      ptr %heap, i32 %heap_count, i32 %descriptor_index)
  %data = extractvalue %feme.cpu.rt.Loaded %desc, 0
  %size = extractvalue %feme.cpu.rt.Loaded %desc, 1
  %kind = extractvalue %feme.cpu.rt.Loaded %desc, 4
  %flags = extractvalue %feme.cpu.rt.Loaded %desc, 5
  %ok_raw = call i1 @feme.cpu.rt.check_access(
      i32 %kind, i32 3, i64 %size, i32 %flags, i64 %byte_offset, i64 4)
  %ok_structured = call i1 @feme.cpu.rt.check_access(
      i32 %kind, i32 2, i64 %size, i32 %flags, i64 %byte_offset, i64 4)
  %ok_kind = or i1 %ok_raw, %ok_structured
  %do_access = and i1 %ok_kind, %mask
  br i1 %do_access, label %access, label %zero

access:
  %ptr = getelementptr i8, ptr %data, i64 %byte_offset
  %v = load float, ptr %ptr, align 4
  ret float %v

zero:
  ret float 0.0
}

define void @feme.cpu.resource.store.raw.f32(
    ptr %heap, i32 %heap_count, i32 %descriptor_index, i64 %byte_offset,
    float %value, i1 %mask) alwaysinline {
entry:
  %desc = call %feme.cpu.rt.Loaded @feme.cpu.rt.load_descriptor(
      ptr %heap, i32 %heap_count, i32 %descriptor_index)
  %data = extractvalue %feme.cpu.rt.Loaded %desc, 0
  %size = extractvalue %feme.cpu.rt.Loaded %desc, 1
  %kind = extractvalue %feme.cpu.rt.Loaded %desc, 4
  %flags = extractvalue %feme.cpu.rt.Loaded %desc, 5
  %ok_raw = call i1 @feme.cpu.rt.check_access(
      i32 %kind, i32 3, i64 %size, i32 %flags, i64 %byte_offset, i64 4)
  %ok_structured = call i1 @feme.cpu.rt.check_access(
      i32 %kind, i32 2, i64 %size, i32 %flags, i64 %byte_offset, i64 4)
  %ok_kind = or i1 %ok_raw, %ok_structured
  %uav_bit = and i32 %flags, 1
  %is_uav = icmp ne i32 %uav_bit, 0
  %do_access0 = and i1 %ok_kind, %mask
  %do_access = and i1 %do_access0, %is_uav
  br i1 %do_access, label %access, label %skip

access:
  %ptr = getelementptr i8, ptr %data, i64 %byte_offset
  store float %value, ptr %ptr, align 4
  ret void

skip:
  ret void
}

declare float @llvm.maxnum.f32(float, float)
declare float @llvm.minnum.f32(float, float)
declare float @llvm.round.f32(float)
