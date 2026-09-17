//===-- SPIRVDerivativeOps.td - MLIR SPIR-V Derivative Ops -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains derivative ops for the SPIR-V dialect. It corresponds
// to the "Derivative Instructions" section of the SPIR-V spec.
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_SPIRV_IR_DERIVATIVE_OPS
#define MLIR_DIALECT_SPIRV_IR_DERIVATIVE_OPS

include "mlir/Dialect/SPIRV/IR/SPIRVBase.td"

// -----

class SPIRV_DerivativeOp<string mnemonic, list<Trait> traits = []> :
      SPIRV_Op<mnemonic,
               !listconcat(traits, [AllTypesMatch<["p", "result"]>])> {
  let arguments = (ins
    SPIRV_ScalarOrVectorOf<SPIRV_Float>:$p
  );

  let results = (outs
    SPIRV_ScalarOrVectorOf<SPIRV_Float>:$result
  );

  let assemblyFormat = "operands attr-dict `:` type($result)";

  // No additional verification needed beyond the ODS-generated
  // same-type-as-result check every derivative instruction's own operand
  // and result share.
  let hasVerifier = 0;
}

def SPIRV_DPdxOp : SPIRV_DerivativeOp<"DPdx"> {
  let summary = [{
    Same result as either OpDPdxFine or OpDPdxCoarse on P. Selection of
    which one is based on external factors.
  }];

  let description = [{
    Result Type must be a scalar or vector of floating-point type.

    P must have the same type as Result Type.  It is the value to
    take the derivative of.

    #### Example:

    ```mlir
    %0 = spirv.DPdx %arg0 : f32
    ```
  }];

  let availability = [
    MinVersion<SPIRV_V_1_0>,
    MaxVersion<SPIRV_V_1_6>,
    Extension<[]>,
    Capability<[SPIRV_C_Shader]>
  ];
}

def SPIRV_DPdyOp : SPIRV_DerivativeOp<"DPdy"> {
  let summary = [{
    Same result as either OpDPdyFine or OpDPdyCoarse on P. Selection of
    which one is based on external factors.
  }];

  let description = [{
    Result Type must be a scalar or vector of floating-point type.

    P must have the same type as Result Type.  It is the value to
    take the derivative of.

    #### Example:

    ```mlir
    %0 = spirv.DPdy %arg0 : f32
    ```
  }];

  let availability = [
    MinVersion<SPIRV_V_1_0>,
    MaxVersion<SPIRV_V_1_6>,
    Extension<[]>,
    Capability<[SPIRV_C_Shader]>
  ];
}

def SPIRV_FwidthOp : SPIRV_DerivativeOp<"Fwidth"> {
  let summary = [{
    Result is the same as computing the sum of the absolute values of
    OpDPdx and OpDPdy on P.
  }];

  let description = [{
    Result Type must be a scalar or vector of floating-point type.

    P must have the same type as Result Type.  It is the value to
    take the derivative of.

    #### Example:

    ```mlir
    %0 = spirv.Fwidth %arg0 : f32
    ```
  }];

  let availability = [
    MinVersion<SPIRV_V_1_0>,
    MaxVersion<SPIRV_V_1_6>,
    Extension<[]>,
    Capability<[SPIRV_C_Shader]>
  ];
}

def SPIRV_DPdxFineOp : SPIRV_DerivativeOp<"DPdxFine"> {
  let summary = [{
    Same result as OpDPdx, computed with high precision, for
    P in the region of the pixel(s) local to invocation, contrasted with
    OpDPdxCoarse.
  }];

  let description = [{
    Result Type must be a scalar or vector of floating-point type.

    P must have the same type as Result Type.  It is the value to
    take the derivative of.

    #### Example:

    ```mlir
    %0 = spirv.DPdxFine %arg0 : f32
    ```
  }];

  let availability = [
    MinVersion<SPIRV_V_1_0>,
    MaxVersion<SPIRV_V_1_6>,
    Extension<[]>,
    Capability<[SPIRV_C_DerivativeControl]>
  ];
}

def SPIRV_DPdyFineOp : SPIRV_DerivativeOp<"DPdyFine"> {
  let summary = [{
    Same result as OpDPdy, computed with high precision, for
    P in the region of the pixel(s) local to invocation, contrasted with
    OpDPdyCoarse.
  }];

  let description = [{
    Result Type must be a scalar or vector of floating-point type.

    P must have the same type as Result Type.  It is the value to
    take the derivative of.

    #### Example:

    ```mlir
    %0 = spirv.DPdyFine %arg0 : f32
    ```
  }];

  let availability = [
    MinVersion<SPIRV_V_1_0>,
    MaxVersion<SPIRV_V_1_6>,
    Extension<[]>,
    Capability<[SPIRV_C_DerivativeControl]>
  ];
}

def SPIRV_FwidthFineOp : SPIRV_DerivativeOp<"FwidthFine"> {
  let summary = [{
    Result is the same as computing the sum of the absolute values of
    OpDPdxFine and OpDPdyFine on P.
  }];

  let description = [{
    Result Type must be a scalar or vector of floating-point type.

    P must have the same type as Result Type.  It is the value to
    take the derivative of.

    #### Example:

    ```mlir
    %0 = spirv.FwidthFine %arg0 : f32
    ```
  }];

  let availability = [
    MinVersion<SPIRV_V_1_0>,
    MaxVersion<SPIRV_V_1_6>,
    Extension<[]>,
    Capability<[SPIRV_C_DerivativeControl]>
  ];
}

def SPIRV_DPdxCoarseOp : SPIRV_DerivativeOp<"DPdxCoarse"> {
  let summary = [{
    Same result as OpDPdx, but with only the derivative in x for the
    region containing the pixel(s) local to invocation computed at a
    possibly lower precision, contrasted with OpDPdxFine.
  }];

  let description = [{
    Result Type must be a scalar or vector of floating-point type.

    P must have the same type as Result Type.  It is the value to
    take the derivative of.

    #### Example:

    ```mlir
    %0 = spirv.DPdxCoarse %arg0 : f32
    ```
  }];

  let availability = [
    MinVersion<SPIRV_V_1_0>,
    MaxVersion<SPIRV_V_1_6>,
    Extension<[]>,
    Capability<[SPIRV_C_DerivativeControl]>
  ];
}

def SPIRV_DPdyCoarseOp : SPIRV_DerivativeOp<"DPdyCoarse"> {
  let summary = [{
    Same result as OpDPdy, but with only the derivative in y for the
    region containing the pixel(s) local to invocation computed at a
    possibly lower precision, contrasted with OpDPdyFine.
  }];

  let description = [{
    Result Type must be a scalar or vector of floating-point type.

    P must have the same type as Result Type.  It is the value to
    take the derivative of.

    #### Example:

    ```mlir
    %0 = spirv.DPdyCoarse %arg0 : f32
    ```
  }];

  let availability = [
    MinVersion<SPIRV_V_1_0>,
    MaxVersion<SPIRV_V_1_6>,
    Extension<[]>,
    Capability<[SPIRV_C_DerivativeControl]>
  ];
}

def SPIRV_FwidthCoarseOp : SPIRV_DerivativeOp<"FwidthCoarse"> {
  let summary = [{
    Result is the same as computing the sum of the absolute values of
    OpDPdxCoarse and OpDPdyCoarse on P.
  }];

  let description = [{
    Result Type must be a scalar or vector of floating-point type.

    P must have the same type as Result Type.  It is the value to
    take the derivative of.

    #### Example:

    ```mlir
    %0 = spirv.FwidthCoarse %arg0 : f32
    ```
  }];

  let availability = [
    MinVersion<SPIRV_V_1_0>,
    MaxVersion<SPIRV_V_1_6>,
    Extension<[]>,
    Capability<[SPIRV_C_DerivativeControl]>
  ];
}

#endif // MLIR_DIALECT_SPIRV_IR_DERIVATIVE_OPS
