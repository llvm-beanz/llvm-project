; RUN: llc -O0 -mtriple=spirv-unknown-vulkan-library -stop-after=legalize-spirv-resource-binding %s -o - | FileCheck %s

%Resource = type { target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) }

@resource = internal global %Resource poison
@ambiguous = internal global %Resource poison
@undominated = internal global %Resource poison
@volatile = internal global %Resource poison
@name = private constant [4 x i8] c"Out\00"

; CHECK-NOT: @resource =
; CHECK-LABEL: define void @forward(
; CHECK: %handle = call target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) @llvm.spv.resource.handlefrombinding
; CHECK-NOT: store target(
; CHECK-NOT: load target(
; CHECK: @llvm.spv.resource.getpointer{{.*}}(target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) %handle,
; CHECK: ret void
define void @forward(i32 %index) {
entry:
  %handle = call target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr @name)
  %init = getelementptr inbounds %Resource, ptr @resource, i32 0, i32 0
  store target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) %handle, ptr %init
  br label %use

use:
  %field = getelementptr %Resource, ptr @resource, i32 0, i32 0
  %loaded = load target("spirv.Image", i32, 5, 2, 0, 0, 2, 33), ptr %field
  %p = call ptr @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) %loaded, i32 %index)
  store i32 42, ptr %p
  ret void
}

; CHECK-LABEL: define void @multiple_stores(
; CHECK: store target(
; CHECK: store target(
; CHECK: load target(
define void @multiple_stores(i1 %cond) {
entry:
  %h0 = call target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr @name)
  %h1 = call target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) @llvm.spv.resource.handlefrombinding(i32 0, i32 2, i32 1, i32 0, ptr @name)
  %field = getelementptr %Resource, ptr @ambiguous, i32 0, i32 0
  store target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) %h0, ptr %field
  br i1 %cond, label %overwrite, label %use
overwrite:
  store target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) %h1, ptr %field
  br label %use
use:
  %loaded = load target("spirv.Image", i32, 5, 2, 0, 0, 2, 33), ptr %field
  %p = call ptr @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) %loaded, i32 0)
  store i32 42, ptr %p
  ret void
}

; CHECK-LABEL: define void @not_dominated(
; CHECK: store target(
; CHECK: load target(
define void @not_dominated(i1 %cond) {
entry:
  br i1 %cond, label %init, label %use
init:
  %handle = call target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr @name)
  %dst = getelementptr %Resource, ptr @undominated, i32 0, i32 0
  store target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) %handle, ptr %dst
  br label %use
use:
  %src = getelementptr %Resource, ptr @undominated, i32 0, i32 0
  %loaded = load target("spirv.Image", i32, 5, 2, 0, 0, 2, 33), ptr %src
  %p = call ptr @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) %loaded, i32 0)
  store i32 42, ptr %p
  ret void
}

; CHECK-LABEL: define void @volatile_access(
; CHECK: store volatile target(
; CHECK: load volatile target(
define void @volatile_access() {
entry:
  %handle = call target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr @name)
  %field = getelementptr %Resource, ptr @volatile, i32 0, i32 0
  store volatile target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) %handle, ptr %field
  %loaded = load volatile target("spirv.Image", i32, 5, 2, 0, 0, 2, 33), ptr %field
  %p = call ptr @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) %loaded, i32 0)
  store i32 42, ptr %p
  ret void
}

declare target("spirv.Image", i32, 5, 2, 0, 0, 2, 33) @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 2, 0, 0, 2, 33), i32)
