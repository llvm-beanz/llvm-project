//===- DXSAControlFlowOps.td - DXSA control flow ops ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Control flow instructions of the DXSA dialect. Each op takes no operands
// and marks a boundary of a loop, switch or conditional construct, or
// returns from the program.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIALECT_DXSA_IR_DXSACONTROLFLOWOPS
#define FEME_DIALECT_DXSA_IR_DXSACONTROLFLOWOPS

include "feme/Dialect/DXSA/IR/DXSAOpBase.td"

//===----------------------------------------------------------------------===//
// dxsa.abort
//===----------------------------------------------------------------------===//

def DXSA_Abort : DXSA_NoOperandOp<"abort"> {
  let summary = "aborts shader execution";
  let description = [{
    The `dxsa.abort` operation terminates the current shader invocation
    unconditionally. No further instructions of the current invocation
    are executed and any side effects pending after this point are
    discarded.

    Example:

    ```mlir
    dxsa.abort
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.break
//===----------------------------------------------------------------------===//

def DXSA_Break : DXSA_NoOperandOp<"break"> {
  let summary = "exits the enclosing loop or switch";
  let description = [{
    The `dxsa.break` operation transfers execution to the instruction that
    follows the innermost enclosing loop or switch construct. It is valid
    only inside a loop or inside a case of a switch.

    Example:

    ```mlir
    dxsa.break
    ```
  }];
}

//===----------------------------------------------------------------------===//
// Conditional control flow
//===----------------------------------------------------------------------===//

// Shared base for the control-flow instructions whose only operand is the
// condition their `_z`/`_nz` test is applied to.
class DXSA_ConditionalFlowOp<string mnemonic> : DXSA_Op<mnemonic> {
  let arguments = (ins DXSA_SrcOperandAttr:$cond);
  let results = (outs);
  let assemblyFormat = "$cond attr-dict";
}

//===----------------------------------------------------------------------===//
// dxsa.breakc_z / dxsa.breakc_nz
//===----------------------------------------------------------------------===//

def DXSA_BreakcZ : DXSA_ConditionalFlowOp<"breakc_z"> {
  let summary = "exits the enclosing loop or switch when all condition bits are zero";
  let description = [{
    The `dxsa.breakc_z` operation transfers execution to the instruction
    that follows the innermost enclosing loop or switch construct when
    every bit of the selected component of `$cond` is zero. Otherwise
    execution falls through to the next instruction.

    Example:

    ```mlir
    dxsa.breakc_z r<0, <x>>
    ```
  }];
}

def DXSA_BreakcNz : DXSA_ConditionalFlowOp<"breakc_nz"> {
  let summary = "exits the enclosing loop or switch when any condition bit is nonzero";
  let description = [{
    The `dxsa.breakc_nz` operation transfers execution to the instruction
    that follows the innermost enclosing loop or switch construct when any
    bit of the selected component of `$cond` is nonzero. Otherwise
    execution falls through to the next instruction.

    Example:

    ```mlir
    dxsa.breakc_nz r<0, <x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.continuec_z / dxsa.continuec_nz
//===----------------------------------------------------------------------===//

def DXSA_ContinuecZ : DXSA_ConditionalFlowOp<"continuec_z"> {
  let summary = "restarts the enclosing loop when all condition bits are zero";
  let description = [{
    The `dxsa.continuec_z` operation transfers execution back to the start
    of the innermost enclosing loop when every bit of the selected
    component of `$cond` is zero. Otherwise execution falls through to the
    next instruction.

    Example:

    ```mlir
    dxsa.continuec_z r<0, <x>>
    ```
  }];
}

def DXSA_ContinuecNz : DXSA_ConditionalFlowOp<"continuec_nz"> {
  let summary = "restarts the enclosing loop when any condition bit is nonzero";
  let description = [{
    The `dxsa.continuec_nz` operation transfers execution back to the start
    of the innermost enclosing loop when any bit of the selected component
    of `$cond` is nonzero. Otherwise execution falls through to the next
    instruction.

    Example:

    ```mlir
    dxsa.continuec_nz r<0, <x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.discard_z / dxsa.discard_nz
//===----------------------------------------------------------------------===//

def DXSA_DiscardZ : DXSA_ConditionalFlowOp<"discard_z"> {
  let summary = "discards the pixel when all condition bits are zero";
  let description = [{
    The `dxsa.discard_z` operation marks the current pixel as discarded
    when every bit of the selected component of `$cond` is zero, so that
    none of its output is written. Execution of the remaining instructions
    is unaffected.

    Example:

    ```mlir
    dxsa.discard_z r<0, <x>>
    ```
  }];
}

def DXSA_DiscardNz : DXSA_ConditionalFlowOp<"discard_nz"> {
  let summary = "discards the pixel when any condition bit is nonzero";
  let description = [{
    The `dxsa.discard_nz` operation marks the current pixel as discarded
    when any bit of the selected component of `$cond` is nonzero, so that
    none of its output is written. Execution of the remaining instructions
    is unaffected.

    Example:

    ```mlir
    dxsa.discard_nz r<0, <x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.if_z / dxsa.if_nz
//===----------------------------------------------------------------------===//

def DXSA_IfZ : DXSA_ConditionalFlowOp<"if_z"> {
  let summary = "begins an if construct taken when all condition bits are zero";
  let description = [{
    The `dxsa.if_z` operation begins a conditional construct whose block
    runs when every bit of the selected component of `$cond` is zero. It is
    closed by a matching `dxsa.endif`, optionally with a `dxsa.else` block
    that runs otherwise.

    Example:

    ```mlir
    dxsa.if_z r<0, <x>>
    ```
  }];
}

def DXSA_IfNz : DXSA_ConditionalFlowOp<"if_nz"> {
  let summary = "begins an if construct taken when any condition bit is nonzero";
  let description = [{
    The `dxsa.if_nz` operation begins a conditional construct whose block
    runs when any bit of the selected component of `$cond` is nonzero. It
    is closed by a matching `dxsa.endif`, optionally with a `dxsa.else`
    block that runs otherwise.

    Example:

    ```mlir
    dxsa.if_nz r<0, <x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.retc_z / dxsa.retc_nz
//===----------------------------------------------------------------------===//

def DXSA_RetcZ : DXSA_ConditionalFlowOp<"retc_z"> {
  let summary = "returns when all condition bits are zero";
  let description = [{
    The `dxsa.retc_z` operation returns from the enclosing subroutine, or
    terminates the program, when every bit of the selected component of
    `$cond` is zero. Otherwise execution falls through to the next
    instruction.

    Example:

    ```mlir
    dxsa.retc_z r<0, <x>>
    ```
  }];
}

def DXSA_RetcNz : DXSA_ConditionalFlowOp<"retc_nz"> {
  let summary = "returns when any condition bit is nonzero";
  let description = [{
    The `dxsa.retc_nz` operation returns from the enclosing subroutine, or
    terminates the program, when any bit of the selected component of
    `$cond` is nonzero. Otherwise execution falls through to the next
    instruction.

    Example:

    ```mlir
    dxsa.retc_nz r<0, <x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.switch
//===----------------------------------------------------------------------===//

def DXSA_Switch : DXSA_Op<"switch"> {
  let summary = "begins a switch construct";
  let description = [{
    The `dxsa.switch` operation begins a switch construct that selects the
    `dxsa.case` block whose 32-bit immediate equals the selected component
    of `$selector`, or the `dxsa.default` block when none matches. It is
    closed by a matching `dxsa.endswitch`.

    Example:

    ```mlir
    dxsa.switch r<0, <x>>
    ```
  }];
  let arguments = (ins DXSA_SrcOperandAttr:$selector);
  let results = (outs);
  let assemblyFormat = "$selector attr-dict";
}

//===----------------------------------------------------------------------===//
// dxsa.call
//===----------------------------------------------------------------------===//

def DXSA_Call : DXSA_Op<"call"> {
  let summary = "calls a subroutine identified by a label";
  let description = [{
    The `dxsa.call` operation transfers execution to the subroutine
    marked by `$operand`, a `label` reference. On `dxsa.ret` execution
    resumes at the instruction after this call.

    Example:

    ```mlir
    dxsa.call label<3>
    ```
  }];
  let arguments = (ins DXSA_SrcOperandAttr:$operand);
  let results = (outs);
  let assemblyFormat = "$operand attr-dict";
}

//===----------------------------------------------------------------------===//
// dxsa.callc_z
//===----------------------------------------------------------------------===//

class DXSA_CallcOp<string mnemonic> : DXSA_Op<mnemonic> {
  let arguments = (ins
      DXSA_SrcOperandAttr:$cond,
      DXSA_SrcOperandAttr:$label);
  let results = (outs);
  let assemblyFormat = "$cond `,` $label attr-dict";
}

def DXSA_CallcZ : DXSA_CallcOp<"callc_z"> {
  let summary = "conditionally calls a subroutine when all condition bits are zero";
  let description = [{
    The `dxsa.callc_z` operation transfers execution to the subroutine
    marked by `$label` when every bit of the selected component of
    `$cond` is zero. Otherwise the call is skipped and execution falls
    through to the next instruction. On `dxsa.ret` execution resumes at
    the instruction after this call.

    Example:

    ```mlir
    dxsa.callc_z r<0, <x>>, label<3>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.callc_nz
//===----------------------------------------------------------------------===//

def DXSA_CallcNz : DXSA_CallcOp<"callc_nz"> {
  let summary = "conditionally calls a subroutine when any condition bit is nonzero";
  let description = [{
    The `dxsa.callc_nz` operation transfers execution to the subroutine
    marked by `$label` when any bit of the selected component of `$cond`
    is nonzero. Otherwise the call is skipped and execution falls
    through to the next instruction. On `dxsa.ret` execution resumes at
    the instruction after this call.

    Example:

    ```mlir
    dxsa.callc_nz r<0, <x>>, label<3>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.case
//===----------------------------------------------------------------------===//

def DXSA_Case : DXSA_Op<"case"> {
  let summary = "begins a case block of a switch";
  let description = [{
    The `dxsa.case` operation begins a case block of the enclosing
    switch construct whose label is the 32-bit immediate `$operand`.
    Multiple `dxsa.case` operations may precede the same block to make
    several values share one code path.

    Example:

    ```mlir
    dxsa.case l(0x2A)
    ```
  }];
  let arguments = (ins DXSA_SrcOperandAttr:$operand);
  let results = (outs);
  let assemblyFormat = "$operand attr-dict";
}

//===----------------------------------------------------------------------===//
// dxsa.continue
//===----------------------------------------------------------------------===//

def DXSA_Continue : DXSA_NoOperandOp<"continue"> {
  let summary = "restarts the enclosing loop";
  let description = [{
    The `dxsa.continue` operation transfers execution back to the start of
    the innermost enclosing loop, beginning its next iteration. It is valid
    only inside a loop.

    Example:

    ```mlir
    dxsa.continue
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.debug_break
//===----------------------------------------------------------------------===//

def DXSA_DebugBreak : DXSA_NoOperandOp<"debug_break"> {
  let summary = "signals a breakpoint to an attached shader debugger";
  let description = [{
    The `dxsa.debug_break` operation signals a breakpoint to an attached
    shader debugger. With no debugger attached the operation has no
    observable effect on the shader.

    Example:

    ```mlir
    dxsa.debug_break
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.default
//===----------------------------------------------------------------------===//

def DXSA_Default : DXSA_NoOperandOp<"default"> {
  let summary = "begins the default block of a switch";
  let description = [{
    The `dxsa.default` operation begins the block of the enclosing switch
    construct that runs when no case matches, behaving like `default` in C.
    A switch construct may contain at most one default block.

    Example:

    ```mlir
    dxsa.default
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.else
//===----------------------------------------------------------------------===//

def DXSA_Else : DXSA_NoOperandOp<"else"> {
  let summary = "begins the else block of an if construct";
  let description = [{
    The `dxsa.else` operation begins the optional block of the enclosing if
    construct that runs when its condition is false. It is closed by the
    matching `dxsa.endif`.

    Example:

    ```mlir
    dxsa.else
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.endif
//===----------------------------------------------------------------------===//

def DXSA_Endif : DXSA_NoOperandOp<"endif"> {
  let summary = "closes an if construct";
  let description = [{
    The `dxsa.endif` operation closes the innermost enclosing if construct,
    including its optional `dxsa.else` block.

    Example:

    ```mlir
    dxsa.endif
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.endloop
//===----------------------------------------------------------------------===//

def DXSA_Endloop : DXSA_NoOperandOp<"endloop"> {
  let summary = "closes a loop construct";
  let description = [{
    The `dxsa.endloop` operation closes the innermost enclosing loop
    construct; reaching it transfers execution back to the matching
    `dxsa.loop` to begin the next iteration.

    Example:

    ```mlir
    dxsa.endloop
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.endswitch
//===----------------------------------------------------------------------===//

def DXSA_Endswitch : DXSA_NoOperandOp<"endswitch"> {
  let summary = "closes a switch construct";
  let description = [{
    The `dxsa.endswitch` operation closes the innermost enclosing switch
    construct.

    Example:

    ```mlir
    dxsa.endswitch
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.label
//===----------------------------------------------------------------------===//

def DXSA_Label : DXSA_Op<"label"> {
  let summary = "declares the entry point of a subroutine";
  let description = [{
    The `dxsa.label` operation declares the entry point of a subroutine
    identified by the `label` reference `$operand`. A matching
    `dxsa.call` or `dxsa.callc_z`/`dxsa.callc_nz` transfers execution to
    the instruction that follows.

    Example:

    ```mlir
    dxsa.label label<3>
    ```
  }];
  let arguments = (ins DXSA_DstOperandAttr:$operand);
  let results = (outs);
  let assemblyFormat = "$operand attr-dict";
}

//===----------------------------------------------------------------------===//
// dxsa.loop
//===----------------------------------------------------------------------===//

def DXSA_Loop : DXSA_NoOperandOp<"loop"> {
  let summary = "begins a loop construct";
  let description = [{
    The `dxsa.loop` operation begins a loop construct whose body repeats
    until a `dxsa.break` transfers execution past the matching
    `dxsa.endloop`.

    Example:

    ```mlir
    dxsa.loop
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ret
//===----------------------------------------------------------------------===//

def DXSA_Ret : DXSA_NoOperandOp<"ret"> {
  let summary = "returns from a subroutine or terminates the shader";
  let description = [{
    The `dxsa.ret` operation returns to the instruction after the call when
    executed inside a subroutine, or terminates program execution otherwise.

    Example:

    ```mlir
    dxsa.ret
    ```
  }];
}

#endif // FEME_DIALECT_DXSA_IR_DXSACONTROLFLOWOPS
