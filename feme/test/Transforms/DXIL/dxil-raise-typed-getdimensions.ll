; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes='feme-dxil-raise-ops,verify,feme-dxil-raise-ops,verify' -S %s | FileCheck %s

target triple = "dxil-pc-shadermodel6.6-compute"

%dx.types.Handle = type { ptr }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.Dimensions = type { i32, i32, i32, i32 }

; Only the element-count field of a typed-buffer query is supported.
; CHECK-LABEL: define i32 @scalar_float(
; CHECK: [[H:%.*]] = call target("dx.TypedBuffer", float, 0, 0, 1) @llvm.dx.resource.handlefromheap{{.*}}(i32 %index)
; CHECK: [[N:%.*]] = call i32 @llvm.dx.resource.getdimensions.x{{.*}}(target("dx.TypedBuffer", float, 0, 0, 1) [[H]])
; CHECK-NEXT: ret i32 [[N]]
define i32 @scalar_float(i32 %index) {
  %h0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %index, i1 false, i1 false)
  %h = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h0, %dx.types.ResourceProperties { i32 10, i32 265 })
  %d = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %h, i32 poison)
  %n = extractvalue %dx.types.Dimensions %d, 0
  ret i32 %n
}

; CHECK-LABEL: define i32 @signed_uav(
; CHECK: [[H:%.*]] = call target("dx.TypedBuffer", <3 x i32>, 1, 0, 1) @llvm.dx.resource.handlefromheap{{.*}}(i32 1)
; CHECK: [[N:%.*]] = call i32 @llvm.dx.resource.getdimensions.x{{.*}}(target("dx.TypedBuffer", <3 x i32>, 1, 0, 1) [[H]])
; CHECK-NEXT: ret i32 [[N]]
define i32 @signed_uav() {
  %h0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 1, i1 false, i1 false)
  %h = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h0, %dx.types.ResourceProperties { i32 4106, i32 772 })
  %d = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %h, i32 undef)
  %n = extractvalue %dx.types.Dimensions %d, 0
  ret i32 %n
}

; CHECK-LABEL: define i32 @unsigned_zero_mip(
; CHECK: [[H:%.*]] = call target("dx.TypedBuffer", <4 x i32>, 0, 0, 0) @llvm.dx.resource.handlefromheap{{.*}}(i32 2)
; CHECK: [[N:%.*]] = call i32 @llvm.dx.resource.getdimensions.x{{.*}}(target("dx.TypedBuffer", <4 x i32>, 0, 0, 0) [[H]])
; CHECK-NEXT: ret i32 [[N]]
define i32 @unsigned_zero_mip() {
  %h0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 2, i1 false, i1 false)
  %h = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h0, %dx.types.ResourceProperties { i32 10, i32 1029 })
  %d = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %h, i32 0)
  %n = extractvalue %dx.types.Dimensions %d, 0
  ret i32 %n
}

; Mixed supported/unsupported fields must not partially raise the query.
; CHECK-LABEL: define i32 @mixed_fields(
; CHECK-NOT: call i32 @llvm.dx.resource.getdimensions.x
; CHECK: [[D:%.*]] = call %dx.types.Dimensions @dx.op.getDimensions(
; CHECK: extractvalue %dx.types.Dimensions [[D]], 0
; CHECK: extractvalue %dx.types.Dimensions [[D]], 1
define i32 @mixed_fields() {
  %h0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %h = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h0, %dx.types.ResourceProperties { i32 10, i32 265 })
  %d = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %h, i32 poison)
  %n = extractvalue %dx.types.Dimensions %d, 0
  %stride = extractvalue %dx.types.Dimensions %d, 1
  %sum = add i32 %n, %stride
  ret i32 %sum
}

; CHECK-LABEL: define i32 @field_two(
; CHECK-NOT: call i32 @llvm.dx.resource.getdimensions.x
; CHECK: [[D:%.*]] = call %dx.types.Dimensions @dx.op.getDimensions(
; CHECK: extractvalue %dx.types.Dimensions [[D]], 2
define i32 @field_two() {
  %h0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %h = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h0, %dx.types.ResourceProperties { i32 10, i32 265 })
  %d = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %h, i32 poison)
  %n = extractvalue %dx.types.Dimensions %d, 2
  ret i32 %n
}

; CHECK-LABEL: define %dx.types.Dimensions @aggregate_use(
; CHECK-NOT: call i32 @llvm.dx.resource.getdimensions.x
; CHECK: [[D:%.*]] = call %dx.types.Dimensions @dx.op.getDimensions(
; CHECK-NEXT: ret %dx.types.Dimensions [[D]]
define %dx.types.Dimensions @aggregate_use() {
  %h0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %h = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h0, %dx.types.ResourceProperties { i32 10, i32 265 })
  %d = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %h, i32 poison)
  ret %dx.types.Dimensions %d
}

; A meaningful mip operand has no typed-buffer canonical counterpart.
; CHECK-LABEL: define i32 @dynamic_mip(
; CHECK-NOT: call i32 @llvm.dx.resource.getdimensions.x
; CHECK: call %dx.types.Dimensions @dx.op.getDimensions({{.*}}i32 %mip)
define i32 @dynamic_mip(i32 %mip) {
  %h0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %h = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h0, %dx.types.ResourceProperties { i32 10, i32 265 })
  %d = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %h, i32 %mip)
  %n = extractvalue %dx.types.Dimensions %d, 0
  ret i32 %n
}

; CHECK-LABEL: define i32 @nonzero_mip(
; CHECK-NOT: call i32 @llvm.dx.resource.getdimensions.x
; CHECK: call %dx.types.Dimensions @dx.op.getDimensions({{.*}}i32 1)
define i32 @nonzero_mip() {
  %h0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %h = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h0, %dx.types.ResourceProperties { i32 10, i32 265 })
  %d = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %h, i32 1)
  %n = extractvalue %dx.types.Dimensions %d, 0
  ret i32 %n
}

; CHECK-LABEL: define i32 @raw_query(
; CHECK: [[H:%.*]] = call target("dx.RawBuffer", i8, 0, 0) @llvm.dx.resource.handlefromheap{{.*}}
; CHECK: [[N:%.*]] = call i32 @llvm.dx.resource.getdimensions.x{{.*}}(target("dx.RawBuffer", i8, 0, 0) [[H]])
; CHECK-NEXT: ret i32 [[N]]
define i32 @raw_query() {
  %h0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %h = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h0, %dx.types.ResourceProperties { i32 11, i32 0 })
  %d = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %h, i32 poison)
  %n = extractvalue %dx.types.Dimensions %d, 0
  ret i32 %n
}

; CHECK-LABEL: define i32 @raw_uav_query(
; CHECK: [[H:%.*]] = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap{{.*}}(i32 %index)
; CHECK: [[N:%.*]] = call i32 @llvm.dx.resource.getdimensions.x{{.*}}(target("dx.RawBuffer", i8, 1, 0) [[H]])
; CHECK-NEXT: ret i32 [[N]]
define i32 @raw_uav_query(i32 %index) {
  %h0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %index, i1 false, i1 false)
  %h = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h0, %dx.types.ResourceProperties { i32 4107, i32 0 })
  %d = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %h, i32 0)
  %n = extractvalue %dx.types.Dimensions %d, 0
  ret i32 %n
}

; CHECK-LABEL: define i32 @raw_mixed_fields(
; CHECK-NOT: call i32 @llvm.dx.resource.getdimensions.x
; CHECK: [[D:%.*]] = call %dx.types.Dimensions @dx.op.getDimensions(
; CHECK: extractvalue %dx.types.Dimensions [[D]], 0
; CHECK: extractvalue %dx.types.Dimensions [[D]], 1
define i32 @raw_mixed_fields() {
  %h0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %h = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h0, %dx.types.ResourceProperties { i32 11, i32 0 })
  %d = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %h, i32 poison)
  %n = extractvalue %dx.types.Dimensions %d, 0
  %other = extractvalue %dx.types.Dimensions %d, 1
  %sum = add i32 %n, %other
  ret i32 %sum
}

; CHECK-LABEL: define i32 @raw_nonzero_mip(
; CHECK-NOT: call i32 @llvm.dx.resource.getdimensions.x
; CHECK: call %dx.types.Dimensions @dx.op.getDimensions({{.*}}i32 1)
define i32 @raw_nonzero_mip() {
  %h0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %h = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h0, %dx.types.ResourceProperties { i32 11, i32 0 })
  %d = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %h, i32 1)
  %n = extractvalue %dx.types.Dimensions %d, 0
  ret i32 %n
}

; CHECK-LABEL: define i32 @structured_query(
; CHECK: [[H:%.*]] = call target("dx.RawBuffer", [12 x i8], 0, 0) @llvm.dx.resource.handlefromheap{{.*}}
; CHECK: [[N:%.*]] = call i32 @llvm.dx.resource.getdimensions.x{{.*}}(target("dx.RawBuffer", [12 x i8], 0, 0) [[H]])
; CHECK-NEXT: ret i32 [[N]]
define i32 @structured_query() {
  %h0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %h = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h0, %dx.types.ResourceProperties { i32 12, i32 12 })
  %d = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %h, i32 poison)
  %n = extractvalue %dx.types.Dimensions %d, 0
  ret i32 %n
}

; The HLSL stride is not a second DXIL dimension field.
; CHECK-LABEL: define i32 @structured_mixed_fields(
; CHECK-NOT: call i32 @llvm.dx.resource.getdimensions.x
; CHECK: [[D:%.*]] = call %dx.types.Dimensions @dx.op.getDimensions(
; CHECK: extractvalue %dx.types.Dimensions [[D]], 0
; CHECK: extractvalue %dx.types.Dimensions [[D]], 1
define i32 @structured_mixed_fields() {
  %h0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %h = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h0, %dx.types.ResourceProperties { i32 12, i32 12 })
  %d = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %h, i32 poison)
  %n = extractvalue %dx.types.Dimensions %d, 0
  %other = extractvalue %dx.types.Dimensions %d, 1
  %sum = add i32 %n, %other
  ret i32 %sum
}

; CHECK-LABEL: define i32 @structured_uav_query(
; CHECK: [[H:%.*]] = call target("dx.RawBuffer", [20 x i8], 1, 0) @llvm.dx.resource.handlefromheap{{.*}}(i32 %index)
; CHECK: [[N:%.*]] = call i32 @llvm.dx.resource.getdimensions.x{{.*}}(target("dx.RawBuffer", [20 x i8], 1, 0) [[H]])
; CHECK-NEXT: ret i32 [[N]]
define i32 @structured_uav_query(i32 %index) {
  %h0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %index, i1 false, i1 false)
  %h = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h0, %dx.types.ResourceProperties { i32 4108, i32 20 })
  %d = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %h, i32 0)
  %n = extractvalue %dx.types.Dimensions %d, 0
  ret i32 %n
}

declare %dx.types.Handle @dx.op.createHandleFromHeap(i32, i32, i1, i1)
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties)
declare %dx.types.Dimensions @dx.op.getDimensions(i32, %dx.types.Handle, i32)
