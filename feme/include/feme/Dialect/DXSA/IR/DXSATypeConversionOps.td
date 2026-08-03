//===- DXSATypeConversionOps.td - DXSA type conversion ops *- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Type conversion instructions of the DXSA dialect.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIALECT_DXSA_IR_DXSATYPECONVERSIONOPS
#define FEME_DIALECT_DXSA_IR_DXSATYPECONVERSIONOPS

include "feme/Dialect/DXSA/IR/DXSAOpBase.td"

//===----------------------------------------------------------------------===//
// dxsa.dtof
//===----------------------------------------------------------------------===//

def DXSA_DToF : DXSA_UnaryOp<"dtof"> {
  let summary = "component-wise double-precision to single-precision float conversion";
  let description = [{
    The `dxsa.dtof` operation converts each double-precision component of
    `$src` to a single-precision float using round-to-nearest-even rounding
    and writes the result to `$dst`.

    The source encodes double values as component pairs: (x,y) holds the
    first double (x = low 32 bits, y = high 32 bits) and (z,w) holds the
    second. Valid source swizzles are .xyzw, .xyxy, .zwxy, and .zwzw.
    Valid destination write masks are any one or two components.

    Example:

    ```mlir
    dxsa.dtof r<0, <x, y>>, r<1>
    dxsa.dtof r<0, <x, y>>, -r<1>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dtoi
//===----------------------------------------------------------------------===//

def DXSA_DToI : DXSA_UnaryOp<"dtoi"> {
  let summary = "component-wise double-precision float to signed int32 conversion";
  let description = [{
    The `dxsa.dtoi` operation converts each double-precision component of
    `$src` to a signed 32-bit integer, rounding toward zero, and writes it
    to `$dst`.

    The source encodes double values as component pairs: (x,y) holds the
    first double (x = low 32 bits, y = high 32 bits) and (z,w) holds the
    second. Valid source swizzles are .xyzw, .xyxy, .zwxy, and .zwzw.
    Valid destination write masks are any one or two components.

    Example:

    ```mlir
    dxsa.dtoi r<0, <x, y>>, r<1>
    dxsa.dtoi r<0, <x, y>>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dtou
//===----------------------------------------------------------------------===//

def DXSA_DToU : DXSA_UnaryOp<"dtou"> {
  let summary = "component-wise double-precision float to unsigned int32 conversion";
  let description = [{
    The `dxsa.dtou` operation converts each double-precision component of
    `$src` to an unsigned 32-bit integer, rounding toward zero, and writes
    it to `$dst`.

    The source encodes double values as component pairs: (x,y) holds the
    first double (x = low 32 bits, y = high 32 bits) and (z,w) holds the
    second. Valid source swizzles are .xyzw, .xyxy, .zwxy, and .zwzw.
    Valid destination write masks are any one or two components.

    Example:

    ```mlir
    dxsa.dtou r<0, <x, y>>, r<1>
    dxsa.dtou r<0, <x, y>>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.f16tof32
//===----------------------------------------------------------------------===//

def DXSA_F16ToF32 : DXSA_UnaryOp<"f16tof32"> {
  let summary = "component-wise float16 to float32 conversion";
  let description = [{
    The `dxsa.f16tof32` operation converts the component-wise float16 value
    stored in the low 16 bits of `$src` to float32 and writes it to `$dst`.

    Example:

    ```mlir
    dxsa.f16tof32 r<0>, r<1>
    dxsa.f16tof32 r<0>, -r<1>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.f32tof16
//===----------------------------------------------------------------------===//

def DXSA_F32ToF16 : DXSA_UnaryOp<"f32tof16"> {
  let summary = "component-wise float32 to float16 conversion";
  let description = [{
    The `dxsa.f32tof16` operation converts each float32 component of `$src` to
    float16, places the result in the low 16 bits of the corresponding `$dst`
    component, and sets the upper 16 bits to zero.

    Example:

    ```mlir
    dxsa.f32tof16 r<0>, r<1>
    dxsa.f32tof16 r<0>, -r<1>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ftod
//===----------------------------------------------------------------------===//

def DXSA_FToD : DXSA_UnaryOp<"ftod"> {
  let summary = "component-wise single-precision float to double-precision conversion";
  let description = [{
    The `dxsa.ftod` operation converts each single-precision float component
    of `$src` to a double-precision float and writes the result to `$dst`.

    The destination encodes double values as component pairs: .xy receives
    the first conversion result (x = low 32 bits, y = high 32 bits) and .zw
    receives the second. Valid destination write masks are .xy, .zw, and
    .xyzw. The source provides two float32 components via x and y (z and w
    are ignored after swizzle).

    Example:

    ```mlir
    dxsa.ftod r<0>, r<1>
    dxsa.ftod r<0>, -r<1>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ftoi
//===----------------------------------------------------------------------===//

def DXSA_FToI : DXSA_UnaryOp<"ftoi"> {
  let summary = "component-wise float to signed integer conversion";
  let description = [{
    The `dxsa.ftoi` operation converts each floating-point component of `$src`
    to a signed 32-bit integer, rounding towards zero, and writes it to `$dst`.

    Example:

    ```mlir
    dxsa.ftoi r<0>, r<1>
    dxsa.ftoi r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ftou
//===----------------------------------------------------------------------===//

def DXSA_FToU : DXSA_UnaryOp<"ftou"> {
  let summary = "component-wise float to unsigned integer conversion";
  let description = [{
    The `dxsa.ftou` operation converts each floating-point component of `$src`
    to an unsigned 32-bit integer, rounding towards zero, and writes it to
    `$dst`.

    Example:

    ```mlir
    dxsa.ftou r<0>, r<1>
    dxsa.ftou r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.itod
//===----------------------------------------------------------------------===//

def DXSA_IToD : DXSA_UnaryOp<"itod"> {
  let summary = "component-wise signed int32 to double-precision float conversion";
  let description = [{
    The `dxsa.itod` operation converts each signed 32-bit integer component
    of `$src` to a double-precision float and writes the result to `$dst`.

    The destination encodes double values as component pairs: .xy receives
    the first conversion result (x = low 32 bits, y = high 32 bits) and .zw
    receives the second. Valid destination write masks are .xy, .zw, and
    .xyzw. The source provides two int32 components via x and y (z and w
    are ignored after swizzle).

    Example:

    ```mlir
    dxsa.itod r<0>, r<1>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.itof
//===----------------------------------------------------------------------===//

def DXSA_IToF : DXSA_UnaryOp<"itof"> {
  let summary = "component-wise signed integer to float conversion";
  let description = [{
    The `dxsa.itof` operation converts each signed 32-bit integer component of
    `$src` to floating-point and writes it to `$dst`.

    Example:

    ```mlir
    dxsa.itof r<0>, r<1>
    dxsa.itof r<0>, -r<1>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.utod
//===----------------------------------------------------------------------===//

def DXSA_UToD : DXSA_UnaryOp<"utod"> {
  let summary = "component-wise unsigned int32 to double-precision float conversion";
  let description = [{
    The `dxsa.utod` operation converts each unsigned 32-bit integer component
    of `$src` to a double-precision float and writes the result to `$dst`.

    The destination encodes double values as component pairs: .xy receives
    the first conversion result (x = low 32 bits, y = high 32 bits) and .zw
    receives the second. Valid destination write masks are .xy, .zw, and
    .xyzw. The source provides two uint32 components via x and y (z and w
    are ignored after swizzle).

    Example:

    ```mlir
    dxsa.utod r<0>, r<1>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.utof
//===----------------------------------------------------------------------===//

def DXSA_UToF : DXSA_UnaryOp<"utof"> {
  let summary = "component-wise unsigned integer to float conversion";
  let description = [{
    The `dxsa.utof` operation converts each unsigned 32-bit integer component
    of `$src` to floating-point and writes it to `$dst`.

    Example:

    ```mlir
    dxsa.utof r<0>, r<1>
    ```
  }];
}

#endif // FEME_DIALECT_DXSA_IR_DXSATYPECONVERSIONOPS
