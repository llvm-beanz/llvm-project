//===- IntrinsicsDirectX.td - Defines DirectX intrinsics ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the DirectX-specific intrinsics.
//
//===----------------------------------------------------------------------===//


let TargetPrefix = "dx" in {

def int_dx_create_handle : GCCBuiltin<"__builtin_dx_create_handle">,
    Intrinsic<[llvm_ptr_ty],
    [llvm_i8_ty, llvm_i32_ty, llvm_i32_ty, llvm_i1_ty]>;

foreach ty = [f16, f32, i16, i32] in {
  def int_dx_buffer_load_#ty :
      Intrinsic<[LLVMType<ty>], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty]>;
}

}
