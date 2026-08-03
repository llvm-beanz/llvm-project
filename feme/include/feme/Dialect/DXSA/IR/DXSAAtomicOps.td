//===- DXSAAtomicOps.td - DXSA atomic ops ---------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Atomic instructions of the DXSA dialect.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIALECT_DXSA_IR_DXSAATOMICOPS
#define FEME_DIALECT_DXSA_IR_DXSAATOMICOPS

include "feme/Dialect/DXSA/IR/DXSAOpBase.td"

//===----------------------------------------------------------------------===//
// DXSA shared base for atomic memory ops
//===----------------------------------------------------------------------===//

class DXSA_AtomicOp<string mnemonic> : DXSA_Op<mnemonic> {
  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$dst_address,
      DXSA_SrcOperandAttr:$src0);
  let results = (outs);
  let assemblyFormat = "$dst `,` $dst_address `,` $src0 attr-dict";
}

//===----------------------------------------------------------------------===//
// dxsa.atomic_and
//===----------------------------------------------------------------------===//

def DXSA_AtomicAnd : DXSA_AtomicOp<"atomic_and"> {
  let summary = "atomic bitwise AND to memory";
  let description = [{
    Single component 32-bit bitwise AND of `$src0` into `$dst` at the
    32-bit per-component address `$dst_address`, performed atomically.

    `$dst` must be a UAV, or in a Compute Shader it can also be
    thread group shared memory. The number of components taken from
    `$dst_address` is determined by the dimensionality of `$dst`.

    Example:

    ```mlir
    dxsa.atomic_and u<0>, r<0>, r<1, <x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.atomic_or
//===----------------------------------------------------------------------===//

def DXSA_AtomicOr : DXSA_AtomicOp<"atomic_or"> {
  let summary = "atomic bitwise OR to memory";
  let description = [{
    Single component 32-bit bitwise OR of `$src0` into `$dst` at the
    32-bit per-component address `$dst_address`, performed atomically.

    `$dst` must be a UAV, or in a Compute Shader it can also be
    thread group shared memory. The number of components taken from
    `$dst_address` is determined by the dimensionality of `$dst`.

    Example:

    ```mlir
    dxsa.atomic_or u<0>, r<0>, r<1, <x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.atomic_xor
//===----------------------------------------------------------------------===//

def DXSA_AtomicXor : DXSA_AtomicOp<"atomic_xor"> {
  let summary = "atomic bitwise XOR to memory";
  let description = [{
    Single component 32-bit bitwise XOR of `$src0` into `$dst` at the
    32-bit per-component address `$dst_address`, performed atomically.

    `$dst` must be a UAV, or in a Compute Shader it can also be
    thread group shared memory. The number of components taken from
    `$dst_address` is determined by the dimensionality of `$dst`.

    Example:

    ```mlir
    dxsa.atomic_xor u<0>, r<0>, r<1, <x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.atomic_iadd
//===----------------------------------------------------------------------===//

def DXSA_AtomicIAdd : DXSA_AtomicOp<"atomic_iadd"> {
  let summary = "atomic integer add to memory";
  let description = [{
    Single component 32-bit integer add of `$src0` into `$dst` at the
    32-bit per-component address `$dst_address`, performed atomically.
    Insensitive to sign.

    `$dst` must be a UAV, or in a Compute Shader it can also be
    thread group shared memory. The number of components taken from
    `$dst_address` is determined by the dimensionality of `$dst`.

    Example:

    ```mlir
    dxsa.atomic_iadd u<0>, r<0>, r<1, <x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.atomic_imax
//===----------------------------------------------------------------------===//

def DXSA_AtomicIMax : DXSA_AtomicOp<"atomic_imax"> {
  let summary = "atomic signed integer max to memory";
  let description = [{
    Single component 32-bit signed max of `$src0` into `$dst` at the
    32-bit per-component address `$dst_address`, performed atomically.

    `$dst` must be a UAV, or in a Compute Shader it can also be
    thread group shared memory. The number of components taken from
    `$dst_address` is determined by the dimensionality of `$dst`.

    Example:

    ```mlir
    dxsa.atomic_imax u<0>, r<0>, r<1, <x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.atomic_imin
//===----------------------------------------------------------------------===//

def DXSA_AtomicIMin : DXSA_AtomicOp<"atomic_imin"> {
  let summary = "atomic signed integer min to memory";
  let description = [{
    Single component 32-bit signed min of `$src0` into `$dst` at the
    32-bit per-component address `$dst_address`, performed atomically.

    `$dst` must be a UAV, or in a Compute Shader it can also be
    thread group shared memory. The number of components taken from
    `$dst_address` is determined by the dimensionality of `$dst`.

    Example:

    ```mlir
    dxsa.atomic_imin u<0>, r<0>, r<1, <x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.atomic_umax
//===----------------------------------------------------------------------===//

def DXSA_AtomicUMax : DXSA_AtomicOp<"atomic_umax"> {
  let summary = "atomic unsigned integer max to memory";
  let description = [{
    Single component 32-bit unsigned max of `$src0` into `$dst` at the
    32-bit per-component address `$dst_address`, performed atomically.

    `$dst` must be a UAV, or in a Compute Shader it can also be
    thread group shared memory. The number of components taken from
    `$dst_address` is determined by the dimensionality of `$dst`.

    Example:

    ```mlir
    dxsa.atomic_umax u<0>, r<0>, r<1, <x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.atomic_umin
//===----------------------------------------------------------------------===//

def DXSA_AtomicUMin : DXSA_AtomicOp<"atomic_umin"> {
  let summary = "atomic unsigned integer min to memory";
  let description = [{
    Single component 32-bit unsigned min of `$src0` into `$dst` at the
    32-bit per-component address `$dst_address`, performed atomically.

    `$dst` must be a UAV, or in a Compute Shader it can also be
    thread group shared memory. The number of components taken from
    `$dst_address` is determined by the dimensionality of `$dst`.

    Example:

    ```mlir
    dxsa.atomic_umin u<0>, r<0>, r<1, <x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// DXSA shared base for immediate atomic ops returning the prior value
//===----------------------------------------------------------------------===//

class DXSA_ImmAtomicBinaryOp<string mnemonic> : DXSA_Op<mnemonic> {
  let arguments = (ins
      DXSA_DstOperandAttr:$dst0,
      DXSA_DstOperandAttr:$dst1,
      DXSA_SrcOperandAttr:$dst_address,
      DXSA_SrcOperandAttr:$src0);
  let results = (outs);
  let assemblyFormat =
      "$dst0 `,` $dst1 `,` $dst_address `,` $src0 attr-dict";
}

//===----------------------------------------------------------------------===//
// dxsa.imm_atomic_iadd
//===----------------------------------------------------------------------===//

def DXSA_ImmAtomicIAdd : DXSA_ImmAtomicBinaryOp<"imm_atomic_iadd"> {
  let summary = "atomic integer add to memory, returning the prior value";
  let description = [{
    The `dxsa.imm_atomic_iadd` operation atomically adds `$src0` to the single
    32-bit value in `$dst1` memory at the per-component address `$dst_address`
    and returns the value held there before the add into `$dst0`. The add is
    insensitive to sign.

    `$dst1` must be an UnorderedAccessView (UAV), or in a Compute Shader thread
    group shared memory. The number of components taken from `$dst_address` is
    determined by the dimensionality of `$dst1`.

    Example:

    ```mlir
    dxsa.imm_atomic_iadd r<0, <x>>, u<0>, r<1>, r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.imm_atomic_and
//===----------------------------------------------------------------------===//

def DXSA_ImmAtomicAnd : DXSA_ImmAtomicBinaryOp<"imm_atomic_and"> {
  let summary = "atomic bitwise AND to memory, returning the prior value";
  let description = [{
    The `dxsa.imm_atomic_and` operation atomically computes the bitwise AND of
    `$src0` with the single 32-bit value in `$dst1` memory at the per-component
    address `$dst_address` and returns the value held there before the AND into
    `$dst0`.

    `$dst1` must be an UnorderedAccessView (UAV), or in a Compute Shader thread
    group shared memory. The number of components taken from `$dst_address` is
    determined by the dimensionality of `$dst1`.

    Example:

    ```mlir
    dxsa.imm_atomic_and r<0, <x>>, u<0>, r<1>, r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.imm_atomic_or
//===----------------------------------------------------------------------===//

def DXSA_ImmAtomicOr : DXSA_ImmAtomicBinaryOp<"imm_atomic_or"> {
  let summary = "atomic bitwise OR to memory, returning the prior value";
  let description = [{
    The `dxsa.imm_atomic_or` operation atomically computes the bitwise OR of
    `$src0` with the single 32-bit value in `$dst1` memory at the per-component
    address `$dst_address` and returns the value held there before the OR into
    `$dst0`.

    `$dst1` must be an UnorderedAccessView (UAV), or in a Compute Shader thread
    group shared memory. The number of components taken from `$dst_address` is
    determined by the dimensionality of `$dst1`.

    Example:

    ```mlir
    dxsa.imm_atomic_or r<0, <x>>, u<0>, r<1>, r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.imm_atomic_xor
//===----------------------------------------------------------------------===//

def DXSA_ImmAtomicXor : DXSA_ImmAtomicBinaryOp<"imm_atomic_xor"> {
  let summary = "atomic bitwise XOR to memory, returning the prior value";
  let description = [{
    The `dxsa.imm_atomic_xor` operation atomically computes the bitwise XOR of
    `$src0` with the single 32-bit value in `$dst1` memory at the per-component
    address `$dst_address` and returns the value held there before the XOR into
    `$dst0`.

    `$dst1` must be an UnorderedAccessView (UAV), or in a Compute Shader thread
    group shared memory. The number of components taken from `$dst_address` is
    determined by the dimensionality of `$dst1`.

    Example:

    ```mlir
    dxsa.imm_atomic_xor r<0, <x>>, u<0>, r<1>, r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.imm_atomic_imax
//===----------------------------------------------------------------------===//

def DXSA_ImmAtomicIMax : DXSA_ImmAtomicBinaryOp<"imm_atomic_imax"> {
  let summary = "atomic signed integer max to memory, returning the prior value";
  let description = [{
    The `dxsa.imm_atomic_imax` operation atomically computes the signed integer
    maximum of `$src0` and the single 32-bit value in `$dst1` memory at the
    per-component address `$dst_address` and returns the value held there before
    the operation into `$dst0`.

    `$dst1` must be an UnorderedAccessView (UAV), or in a Compute Shader thread
    group shared memory. The number of components taken from `$dst_address` is
    determined by the dimensionality of `$dst1`.

    Example:

    ```mlir
    dxsa.imm_atomic_imax r<0, <x>>, u<0>, r<1>, r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.imm_atomic_imin
//===----------------------------------------------------------------------===//

def DXSA_ImmAtomicIMin : DXSA_ImmAtomicBinaryOp<"imm_atomic_imin"> {
  let summary = "atomic signed integer min to memory, returning the prior value";
  let description = [{
    The `dxsa.imm_atomic_imin` operation atomically computes the signed integer
    minimum of `$src0` and the single 32-bit value in `$dst1` memory at the
    per-component address `$dst_address` and returns the value held there before
    the operation into `$dst0`.

    `$dst1` must be an UnorderedAccessView (UAV), or in a Compute Shader thread
    group shared memory. The number of components taken from `$dst_address` is
    determined by the dimensionality of `$dst1`.

    Example:

    ```mlir
    dxsa.imm_atomic_imin r<0, <x>>, u<0>, r<1>, r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.imm_atomic_umax
//===----------------------------------------------------------------------===//

def DXSA_ImmAtomicUMax : DXSA_ImmAtomicBinaryOp<"imm_atomic_umax"> {
  let summary =
      "atomic unsigned integer max to memory, returning the prior value";
  let description = [{
    The `dxsa.imm_atomic_umax` operation atomically computes the unsigned
    integer maximum of `$src0` and the single 32-bit value in `$dst1` memory at
    the per-component address `$dst_address` and returns the value held there
    before the operation into `$dst0`.

    `$dst1` must be an UnorderedAccessView (UAV), or in a Compute Shader thread
    group shared memory. The number of components taken from `$dst_address` is
    determined by the dimensionality of `$dst1`.

    Example:

    ```mlir
    dxsa.imm_atomic_umax r<0, <x>>, u<0>, r<1>, r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.imm_atomic_umin
//===----------------------------------------------------------------------===//

def DXSA_ImmAtomicUMin : DXSA_ImmAtomicBinaryOp<"imm_atomic_umin"> {
  let summary =
      "atomic unsigned integer min to memory, returning the prior value";
  let description = [{
    The `dxsa.imm_atomic_umin` operation atomically computes the unsigned
    integer minimum of `$src0` and the single 32-bit value in `$dst1` memory at
    the per-component address `$dst_address` and returns the value held there
    before the operation into `$dst0`.

    `$dst1` must be an UnorderedAccessView (UAV), or in a Compute Shader thread
    group shared memory. The number of components taken from `$dst_address` is
    determined by the dimensionality of `$dst1`.

    Example:

    ```mlir
    dxsa.imm_atomic_umin r<0, <x>>, u<0>, r<1>, r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.atomic_cmp_store
//===----------------------------------------------------------------------===//

def DXSA_AtomicCmpStore : DXSA_Op<"atomic_cmp_store"> {
  let summary = "atomic compare and conditional write to memory";
  let description = [{
    The `dxsa.atomic_cmp_store` operation atomically compares `$src0` with the
    single 32-bit value in `$dst` memory at the per-component address
    `$dst_address`. If the values are equal, `$src1` is written to that
    location, otherwise the location is left unchanged. Nothing is returned to
    the shader.

    `$dst` must be an UnorderedAccessView (UAV), or in a Compute Shader thread
    group shared memory. The number of components taken from `$dst_address` is
    determined by the dimensionality of `$dst`.

    Example:

    ```mlir
    dxsa.atomic_cmp_store u<0>, r<1>, r<2>, r<3>
    ```
  }];
  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$dst_address,
      DXSA_SrcOperandAttr:$src0,
      DXSA_SrcOperandAttr:$src1);
  let results = (outs);
  let assemblyFormat =
      "$dst `,` $dst_address `,` $src0 `,` $src1 attr-dict";
}

//===----------------------------------------------------------------------===//
// dxsa.imm_atomic_exch
//===----------------------------------------------------------------------===//

def DXSA_ImmAtomicExch : DXSA_ImmAtomicBinaryOp<"imm_atomic_exch"> {
  let summary = "atomic exchange to memory, returning the prior value";
  let description = [{
    The `dxsa.imm_atomic_exch` operation atomically writes `$src0` to the single
    32-bit value in `$dst1` memory at the per-component address `$dst_address`
    and returns the value held there before the write into `$dst0`.

    `$dst1` must be an UnorderedAccessView (UAV), or in a Compute Shader thread
    group shared memory. The number of components taken from `$dst_address` is
    determined by the dimensionality of `$dst1`.

    Example:

    ```mlir
    dxsa.imm_atomic_exch r<0, <x>>, u<0>, r<1>, r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.imm_atomic_cmp_exch
//===----------------------------------------------------------------------===//

def DXSA_ImmAtomicCmpExch : DXSA_Op<"imm_atomic_cmp_exch"> {
  let summary =
      "atomic compare and exchange to memory, returning the prior value";
  let description = [{
    The `dxsa.imm_atomic_cmp_exch` operation atomically compares `$src0` with
    the single 32-bit value in `$dst1` memory at the per-component address
    `$dst_address`. If the values are equal, `$src1` is written to that
    location, otherwise the location is left unchanged. The value held there
    before the operation is always returned into `$dst0`.

    `$dst1` must be an UnorderedAccessView (UAV), or in a Compute Shader thread
    group shared memory. The number of components taken from `$dst_address` is
    determined by the dimensionality of `$dst1`.

    Example:

    ```mlir
    dxsa.imm_atomic_cmp_exch r<0, <x>>, u<0>, r<1>, r<2>, r<3>
    ```
  }];
  let arguments = (ins
      DXSA_DstOperandAttr:$dst0,
      DXSA_DstOperandAttr:$dst1,
      DXSA_SrcOperandAttr:$dst_address,
      DXSA_SrcOperandAttr:$src0,
      DXSA_SrcOperandAttr:$src1);
  let results = (outs);
  let assemblyFormat =
      "$dst0 `,` $dst1 `,` $dst_address `,` $src0 `,` $src1 attr-dict";
}

//===----------------------------------------------------------------------===//
// DXSA shared base for UnorderedAccessView (UAV) counter atomics
//===----------------------------------------------------------------------===//

class DXSA_ImmAtomicCounterOp<string mnemonic> : DXSA_Op<mnemonic> {
  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$uav);
  let results = (outs);
  let assemblyFormat = "$dst `,` $uav attr-dict";
}

//===----------------------------------------------------------------------===//
// dxsa.imm_atomic_alloc
//===----------------------------------------------------------------------===//

def DXSA_ImmAtomicAlloc : DXSA_ImmAtomicCounterOp<"imm_atomic_alloc"> {
  let summary =
      "atomic increment of an UnorderedAccessView (UAV) counter, returning the "
      "prior value";
  let description = [{
    The `dxsa.imm_atomic_alloc` operation atomically increments the hidden
    32-bit counter of the append or count UnorderedAccessView (UAV)
    `$uav` and writes the value the counter held before the increment
    into `$dst`.

    `$uav` must be a structured buffer UAV declared with a counter. The
    counter wraps on overflow.

    Example:

    ```mlir
    dxsa.imm_atomic_alloc r<0, <x>>, u<0>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.imm_atomic_consume
//===----------------------------------------------------------------------===//

def DXSA_ImmAtomicConsume : DXSA_ImmAtomicCounterOp<"imm_atomic_consume"> {
  let summary =
      "atomic decrement of an UnorderedAccessView (UAV) counter, returning the "
      "new value";
  let description = [{
    The `dxsa.imm_atomic_consume` operation atomically decrements the hidden
    32-bit counter of the append or count UnorderedAccessView (UAV)
    `$uav` and writes the new (post-decrement) value into `$dst`.

    `$uav` must be a structured buffer UAV declared with a counter. The
    counter wraps on underflow.

    Example:

    ```mlir
    dxsa.imm_atomic_consume r<0, <x>>, u<0>
    ```
  }];
}

#endif // FEME_DIALECT_DXSA_IR_DXSAATOMICOPS
