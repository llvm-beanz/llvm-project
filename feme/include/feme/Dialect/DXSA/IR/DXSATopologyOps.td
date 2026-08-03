//===- DXSATopologyOps.td - DXSA topology ops --------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Topology instructions of the DXSA dialect.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIALECT_DXSA_IR_DXSATOPOLOGYOPS
#define FEME_DIALECT_DXSA_IR_DXSATOPOLOGYOPS

include "feme/Dialect/DXSA/IR/DXSAOpBase.td"

//===----------------------------------------------------------------------===//
// dxsa.emit
//===----------------------------------------------------------------------===//

def DXSA_Emit : DXSA_NoOperandOp<"emit"> {
  let summary = "outputs a geometry-shader vertex from declared output registers";
  let description = [{
    The `dxsa.emit` operation reads every declared `o#` output register and
    outputs a vertex from the geometry shader. Each `dxsa.emit` produces one
    vertex; successive emits assemble primitives according to the output
    topology declared by `dxsa.dcl_output_topology`.

    A geometry shader may issue `dxsa.emit` any number of times, including
    inside flow control. If output streams have been declared with
    `dxsa.dcl_stream`, `dxsa.emit_stream` must be used instead.

    Example:

    ```mlir
    dxsa.emit
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.cut
//===----------------------------------------------------------------------===//

def DXSA_Cut : DXSA_NoOperandOp<"cut"> {
  let summary = "ends the current geometry-shader output strip";
  let description = [{
    The `dxsa.cut` operation ends the current output strip at the last emitted
    vertex, so the next `dxsa.emit` begins a new strip. This only affects
    linestrip and trianglestrip output topologies; for pointlist output,
    `dxsa.cut` has no effect.

    A geometry shader may issue `dxsa.cut` any number of times, including
    inside flow control. If output streams have been declared with
    `dxsa.dcl_stream`, `dxsa.cut_stream` must be used instead.

    Example:

    ```mlir
    dxsa.cut
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.emit_then_cut
//===----------------------------------------------------------------------===//

def DXSA_EmitThenCut : DXSA_NoOperandOp<"emit_then_cut"> {
  let summary = "outputs a geometry-shader vertex and ends the current strip";
  let description = [{
    The `dxsa.emit_then_cut` operation has the same effect as `dxsa.emit`,
    immediately followed by `dxsa.cut`.

    A geometry shader may issue `dxsa.emit_then_cut` any number of times,
    including inside flow control. If output streams have been declared with
    `dxsa.dcl_stream`, `dxsa.emit_then_cut_stream` must be used instead.

    Example:

    ```mlir
    dxsa.emit_then_cut
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.emit_stream
//===----------------------------------------------------------------------===//

def DXSA_EmitStream : DXSA_GsStreamIndexOp<"emit_stream"> {
  let summary = "outputs a geometry-shader vertex on an output stream";
  let description = [{
    The `dxsa.emit_stream` operation reads every declared `o#` output register
    for stream `$index` and outputs a vertex on that geometry-shader output
    stream. Each `dxsa.emit_stream` produces one vertex; successive emits on
    the same stream assemble primitives according to the output topology
    declared for that stream.

    The `$index` must be in the range [0, 3].

    Example:

    ```mlir
    dxsa.emit_stream 0
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.cut_stream
//===----------------------------------------------------------------------===//

def DXSA_CutStream : DXSA_GsStreamIndexOp<"cut_stream"> {
  let summary = "ends the current strip on a geometry-shader output stream";
  let description = [{
    The `dxsa.cut_stream` operation ends the current output strip at the last
    emitted vertex on stream `$index`, so the next emit on that stream begins a
    new strip. This has the same semantics as `dxsa.cut`, scoped to the given
    output stream.

    The `$index` must be in the range [0, 3].

    Example:

    ```mlir
    dxsa.cut_stream 0
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.emit_then_cut_stream
//===----------------------------------------------------------------------===//

def DXSA_EmitThenCutStream : DXSA_GsStreamIndexOp<"emit_then_cut_stream"> {
  let summary = "outputs a vertex on a stream and ends the current strip";
  let description = [{
    The `dxsa.emit_then_cut_stream` operation has the same effect as
    `dxsa.emit_stream`, immediately followed by `dxsa.cut_stream`, on stream
    `$index`.

    The `$index` must be in the range [0, 3].

    Example:

    ```mlir
    dxsa.emit_then_cut_stream 0
    ```
  }];
}

#endif // FEME_DIALECT_DXSA_IR_DXSATOPOLOGYOPS
