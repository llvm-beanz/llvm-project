//===----------- BinaryParser.cpp - Parse DXSA binary to MLIR -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/DXSA/BinaryParser.h"
#include "feme/Dialect/DXSA/IR/DXSA.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Location.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugLog.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/LogicalResult.h"
#include "llvm/Support/raw_ostream.h"

#include <array>
#include <optional>
#include <tuple>

// d3d12TokenizedProgramFormat.hpp references the `UINT` type in some DECODE_*
// macros. Mirror the Windows SDK alias (`typedef unsigned int UINT`) to use the
// header without modification.
using UINT = unsigned int;
#include "d3d12TokenizedProgramFormat.hpp" // NOLINT

#define DEBUG_TYPE "import-dxsa-bin"

using namespace mlir;
using namespace llvm;
using namespace feme;

#define FAILURE_IF_FAILED(RES)                                                 \
  if (failed(RES))                                                             \
    return failure();

enum OpcodeClass {
  D3D10_SB_FLOAT_OP,
  D3D10_SB_INT_OP,
  D3D10_SB_UINT_OP,
  D3D10_SB_BIT_OP,
  D3D10_SB_FLOW_OP,
  D3D10_SB_TEX_OP,
  D3D10_SB_DCL_OP,
  D3D11_SB_ATOMIC_OP,
  D3D11_SB_MEM_OP,
  D3D11_SB_DOUBLE_OP,
  D3D11_SB_FLOAT_TO_DOUBLE_OP,
  D3D11_SB_DOUBLE_TO_FLOAT_OP,
  D3D11_SB_DEBUG_OP,
};

struct InstructionInfo {
  unsigned numOperands;
  StringRef name;
  OpcodeClass opClass;
  uint32_t precisionFromOutMask;
};

static void initInstructionInfo(MutableArrayRef<InstructionInfo> instructions) {
#define SET(OpCode, Name, NumOperands, PrecMask, OpClass)                      \
  instructions[OpCode] = InstructionInfo{NumOperands, Name, OpClass, PrecMask};
  // clang-format off
  SET(D3D10_SB_OPCODE_ADD, "add", 3, 0x06, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_AND, "and", 3, 0x06, D3D10_SB_BIT_OP);
  SET(D3D10_SB_OPCODE_BREAK, "break", 0, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_BREAKC, "breakc", 1, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_CALL, "call", 1, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_CALLC, "callc", 2, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_CONTINUE, "continue", 0, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_CONTINUEC, "continuec", 1, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_CASE, "case", 1, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_CUT, "cut", 0, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_DEFAULT, "default", 0, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_DISCARD, "discard", 1, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_DIV, "div", 3, 0x06, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_DP2, "dp2", 3, 0x06, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_DP3, "dp3", 3, 0x06, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_DP4, "dp4", 3, 0x06, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_ELSE, "else", 0, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_EMIT, "emit", 0, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_EMITTHENCUT, "emit_then_cut", 0, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_ENDIF, "endif", 0, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_ENDLOOP, "endloop", 0, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_ENDSWITCH, "endswitch", 0, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_EQ, "eq", 3, 0x00, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_EXP, "exp", 2, 0x02, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_FRC, "frc", 2, 0x02, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_FTOI, "ftoi", 2, 0x00, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_FTOU, "ftou", 2, 0x00, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_GE, "ge", 3, 0x00, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_DERIV_RTX, "deriv_rtx", 2, 0x02, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_DERIV_RTY, "deriv_rty", 2, 0x02, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_IADD, "iadd", 3, 0x06, D3D10_SB_INT_OP);
  SET(D3D10_SB_OPCODE_IF, "if", 1, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_IEQ, "ieq", 3, 0x00, D3D10_SB_INT_OP);
  SET(D3D10_SB_OPCODE_IGE, "ige", 3, 0x00, D3D10_SB_INT_OP);
  SET(D3D10_SB_OPCODE_ILT, "ilt", 3, 0x00, D3D10_SB_INT_OP);
  SET(D3D10_SB_OPCODE_IMAD, "imad", 4, 0x0e, D3D10_SB_INT_OP);
  SET(D3D10_SB_OPCODE_IMAX, "imax", 3, 0x06, D3D10_SB_INT_OP);
  SET(D3D10_SB_OPCODE_IMIN, "imin", 3, 0x06, D3D10_SB_INT_OP);
  SET(D3D10_SB_OPCODE_IMUL, "imul", 4, 0x0c, D3D10_SB_INT_OP);
  SET(D3D10_SB_OPCODE_INE, "ine", 3, 0x00, D3D10_SB_INT_OP);
  SET(D3D10_SB_OPCODE_INEG, "ineg", 2, 0x02, D3D10_SB_INT_OP);
  SET(D3D10_SB_OPCODE_ISHL, "ishl", 3, 0x02, D3D10_SB_INT_OP);
  SET(D3D10_SB_OPCODE_ISHR, "ishr", 3, 0x02, D3D10_SB_INT_OP);
  SET(D3D10_SB_OPCODE_ITOF, "itof", 2, 0x00, D3D10_SB_INT_OP);
  SET(D3D10_SB_OPCODE_LABEL, "label", 1, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_LD, "ld", 3, 0x00, D3D10_SB_TEX_OP);
  SET(D3D10_SB_OPCODE_LD_MS, "ldms", 4, 0x00, D3D10_SB_TEX_OP);
  SET(D3D10_SB_OPCODE_LOG, "log", 2, 0x02, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_LOOP, "loop", 0, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_LT, "lt", 3, 0x00, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_MAD, "mad", 4, 0x0e, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_MAX, "max", 3, 0x06, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_MIN, "min", 3, 0x06, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_MOV, "mov", 2, 0x02, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_MOVC, "movc", 4, 0x0c, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_MUL, "mul", 3, 0x06, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_NE, "ne", 3, 0x00, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_NOP, "nop", 0, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_NOT, "not", 2, 0x02, D3D10_SB_BIT_OP);
  SET(D3D10_SB_OPCODE_OR, "or", 3, 0x06, D3D10_SB_BIT_OP);
  SET(D3D10_SB_OPCODE_RESINFO, "resinfo", 3, 0x00, D3D10_SB_TEX_OP);
  SET(D3D10_SB_OPCODE_RET, "ret", 0, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_RETC, "retc", 1, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_ROUND_NE, "round_ne", 2, 0x02, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_ROUND_NI, "round_ni", 2, 0x02, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_ROUND_PI, "round_pi", 2, 0x02, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_ROUND_Z, "round_z", 2, 0x02, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_RSQ, "rsq", 2, 0x02, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_SAMPLE, "sample", 4, 0x00, D3D10_SB_TEX_OP);
  SET(D3D10_SB_OPCODE_SAMPLE_B, "sample_b", 5, 0x00, D3D10_SB_TEX_OP);
  SET(D3D10_SB_OPCODE_SAMPLE_L, "sample_l", 5, 0x00, D3D10_SB_TEX_OP);
  SET(D3D10_SB_OPCODE_SAMPLE_D, "sample_d", 6, 0x00, D3D10_SB_TEX_OP);
  SET(D3D10_SB_OPCODE_SAMPLE_C, "sample_c", 5, 0x00, D3D10_SB_TEX_OP);
  SET(D3D10_SB_OPCODE_SAMPLE_C_LZ, "sample_c_lz", 5, 0x00, D3D10_SB_TEX_OP);
  SET(D3D10_SB_OPCODE_SQRT, "sqrt", 2, 0x02, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_SWITCH, "switch", 1, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_SINCOS, "sincos", 3, 0x04, D3D10_SB_FLOAT_OP);
  SET(D3D10_SB_OPCODE_UDIV, "udiv", 4, 0x0c, D3D10_SB_UINT_OP);
  SET(D3D10_SB_OPCODE_ULT, "ult", 3, 0x00, D3D10_SB_UINT_OP);
  SET(D3D10_SB_OPCODE_UGE, "uge", 3, 0x00, D3D10_SB_UINT_OP);
  SET(D3D10_SB_OPCODE_UMAX, "umax", 3, 0x06, D3D10_SB_UINT_OP);
  SET(D3D10_SB_OPCODE_UMIN, "umin", 3, 0x06, D3D10_SB_UINT_OP);
  SET(D3D10_SB_OPCODE_UMUL, "umul", 4, 0x0c, D3D10_SB_UINT_OP);
  SET(D3D10_SB_OPCODE_UMAD, "umad", 4, 0x0e, D3D10_SB_UINT_OP);
  SET(D3D10_SB_OPCODE_USHR, "ushr", 3, 0x02, D3D10_SB_UINT_OP);
  SET(D3D10_SB_OPCODE_UTOF, "utof", 2, 0x00, D3D10_SB_UINT_OP);
  SET(D3D10_SB_OPCODE_XOR, "xor", 3, 0x06, D3D10_SB_BIT_OP);
  SET(D3D10_SB_OPCODE_RESERVED0, "jmp", 0, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D10_SB_OPCODE_DCL_INPUT, "dcl_input", 1, 0x00, D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_OUTPUT, "dcl_output", 1, 0x00, D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_INPUT_SGV, "dcl_input_sgv", 1, 0x00, D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_INPUT_PS_SGV, "dcl_input_ps_sgv", 1, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_GS_INPUT_PRIMITIVE, "dcl_inputprimitive", 0, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_GS_OUTPUT_PRIMITIVE_TOPOLOGY, "dcl_outputtopology", 0,
      0x00, D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_MAX_OUTPUT_VERTEX_COUNT,
      "dcl_max_output_vertex_count", 0, 0x00, D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_INPUT_PS, "dcl_input_ps", 1, 0x00, D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER, "dcl_constantbuffer", 1, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_SAMPLER, "dcl_sampler", 1, 0x00, D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_RESOURCE, "dcl_resource", 1, 0x00, D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_INPUT_SIV, "dcl_input_siv", 1, 0x00, D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_INPUT_PS_SIV, "dcl_input_ps_siv", 1, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_OUTPUT_SIV, "dcl_output_siv", 1, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_OUTPUT_SGV, "dcl_output_sgv", 1, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_TEMPS, "dcl_temps", 0, 0x00, D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_INDEXABLE_TEMP, "dcl_indexableTemp", 0, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_INDEX_RANGE, "dcl_indexrange", 1, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS, "dcl_globalFlags", 0, 0x00,
      D3D10_SB_DCL_OP);

  SET(D3D10_1_SB_OPCODE_SAMPLE_INFO, "sampleinfo", 2, 0x00, D3D10_SB_TEX_OP);
  SET(D3D10_1_SB_OPCODE_SAMPLE_POS, "samplepos", 3, 0x00, D3D10_SB_TEX_OP);
  SET(D3D10_1_SB_OPCODE_GATHER4, "gather4", 4, 0x00, D3D10_SB_TEX_OP);
  SET(D3D10_1_SB_OPCODE_LOD, "lod", 4, 0x00, D3D10_SB_TEX_OP);

  SET(D3D11_SB_OPCODE_EMIT_STREAM, "emit_stream", 1, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D11_SB_OPCODE_CUT_STREAM, "cut_stream", 1, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D11_SB_OPCODE_EMITTHENCUT_STREAM, "emit_then_cut_stream", 1, 0x00,
      D3D10_SB_FLOW_OP);
  SET(D3D11_SB_OPCODE_INTERFACE_CALL, "fcall", 1, 0x00, D3D10_SB_FLOW_OP);

  SET(D3D11_SB_OPCODE_DCL_STREAM, "dcl_stream", 1, 0x00, D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_FUNCTION_BODY, "dcl_function_body", 0, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_FUNCTION_TABLE, "dcl_function_table", 0, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_INTERFACE, "dcl_interface", 0, 0x00, D3D10_SB_DCL_OP);

  SET(D3D11_SB_OPCODE_BUFINFO, "bufinfo", 2, 0x00, D3D10_SB_TEX_OP);
  SET(D3D11_SB_OPCODE_DERIV_RTX_COARSE, "deriv_rtx_coarse", 2, 0x02,
      D3D10_SB_FLOAT_OP);
  SET(D3D11_SB_OPCODE_DERIV_RTX_FINE, "deriv_rtx_fine", 2, 0x02,
      D3D10_SB_FLOAT_OP);
  SET(D3D11_SB_OPCODE_DERIV_RTY_COARSE, "deriv_rty_coarse", 2, 0x02,
      D3D10_SB_FLOAT_OP);
  SET(D3D11_SB_OPCODE_DERIV_RTY_FINE, "deriv_rty_fine", 2, 0x02,
      D3D10_SB_FLOAT_OP);
  SET(D3D11_SB_OPCODE_GATHER4_C, "gather4_c", 5, 0x00, D3D10_SB_TEX_OP);
  SET(D3D11_SB_OPCODE_GATHER4_PO, "gather4_po", 5, 0x00, D3D10_SB_TEX_OP);
  SET(D3D11_SB_OPCODE_GATHER4_PO_C, "gather4_po_c", 6, 0x00, D3D10_SB_TEX_OP);
  SET(D3D11_SB_OPCODE_RCP, "rcp", 2, 0x02, D3D10_SB_FLOAT_OP);
  SET(D3D11_SB_OPCODE_F32TOF16, "f32tof16", 2, 0x00, D3D10_SB_FLOAT_OP);
  SET(D3D11_SB_OPCODE_F16TOF32, "f16tof32", 2, 0x00, D3D10_SB_FLOAT_OP);
  SET(D3D11_SB_OPCODE_UADDC, "uaddc", 4, 0x0c, D3D10_SB_UINT_OP);
  SET(D3D11_SB_OPCODE_USUBB, "usubb", 4, 0x0c, D3D10_SB_UINT_OP);
  SET(D3D11_SB_OPCODE_COUNTBITS, "countbits", 2, 0x02, D3D10_SB_BIT_OP);
  SET(D3D11_SB_OPCODE_FIRSTBIT_HI, "firstbit_hi", 2, 0x02, D3D10_SB_BIT_OP);
  SET(D3D11_SB_OPCODE_FIRSTBIT_LO, "firstbit_lo", 2, 0x02, D3D10_SB_BIT_OP);
  SET(D3D11_SB_OPCODE_FIRSTBIT_SHI, "firstbit_shi", 2, 0x02, D3D10_SB_BIT_OP);
  SET(D3D11_SB_OPCODE_UBFE, "ubfe", 4, 0x02, D3D10_SB_BIT_OP);
  SET(D3D11_SB_OPCODE_IBFE, "ibfe", 4, 0x02, D3D10_SB_BIT_OP);
  SET(D3D11_SB_OPCODE_BFI, "bfi", 5, 0x02, D3D10_SB_BIT_OP);
  SET(D3D11_SB_OPCODE_BFREV, "bfrev", 2, 0x02, D3D10_SB_BIT_OP);
  SET(D3D11_SB_OPCODE_SWAPC, "swapc", 5, 0x02, D3D10_SB_FLOAT_OP);

  SET(D3D11_SB_OPCODE_HS_DECLS, "hs_decls", 0, 0x00, D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_HS_CONTROL_POINT_PHASE, "hs_control_point_phase", 0, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_HS_FORK_PHASE, "hs_fork_phase", 0, 0x00, D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_HS_JOIN_PHASE, "hs_join_phase", 0, 0x00, D3D10_SB_DCL_OP);

  SET(D3D11_SB_OPCODE_DCL_INPUT_CONTROL_POINT_COUNT,
      "dcl_input_control_point_count", 0, 0x00, D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_OUTPUT_CONTROL_POINT_COUNT,
      "dcl_output_control_point_count", 0, 0x00, D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_TESS_DOMAIN, "dcl_tessellator_domain", 0, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_TESS_PARTITIONING, "dcl_tessellator_partitioning", 0,
      0x00, D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_TESS_OUTPUT_PRIMITIVE,
      "dcl_tessellator_output_primitive", 0, 0x00, D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_HS_MAX_TESSFACTOR, "dcl_hs_max_tessfactor", 0, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_HS_FORK_PHASE_INSTANCE_COUNT,
      "dcl_hs_fork_phase_instance_count", 0, 0x00, D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_HS_JOIN_PHASE_INSTANCE_COUNT,
      "dcl_hs_join_phase_instance_count", 0, 0x00, D3D10_SB_DCL_OP);

  SET(D3D11_SB_OPCODE_DCL_THREAD_GROUP, "dcl_thread_group", 0, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_TYPED, "dcl_uav_typed", 1, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW, "dcl_uav_raw", 1, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_STRUCTURED,
      "dcl_uav_structured", 1, 0x00, D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_THREAD_GROUP_SHARED_MEMORY_RAW, "dcl_tgsm_raw", 1,
      0x00, D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_THREAD_GROUP_SHARED_MEMORY_STRUCTURED,
      "dcl_tgsm_structured", 1, 0x00, D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_RESOURCE_RAW, "dcl_resource_raw", 1, 0x00,
      D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED, "dcl_resource_structured", 1,
      0x00, D3D10_SB_DCL_OP);
  SET(D3D11_SB_OPCODE_LD_UAV_TYPED, "ld_uav_typed", 3, 0x00, D3D11_SB_MEM_OP);
  SET(D3D11_SB_OPCODE_STORE_UAV_TYPED, "store_uav_typed", 3, 0x00,
      D3D11_SB_MEM_OP);
  SET(D3D11_SB_OPCODE_LD_RAW, "ld_raw", 3, 0x00, D3D11_SB_MEM_OP);
  SET(D3D11_SB_OPCODE_STORE_RAW, "store_raw", 3, 0x00, D3D11_SB_MEM_OP);
  SET(D3D11_SB_OPCODE_LD_STRUCTURED, "ld_structured", 4, 0x00, D3D11_SB_MEM_OP);
  SET(D3D11_SB_OPCODE_STORE_STRUCTURED, "store_structured", 4, 0x00,
      D3D11_SB_MEM_OP);
  SET(D3D11_SB_OPCODE_ATOMIC_AND, "atomic_and", 3, 0x00, D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_ATOMIC_OR, "atomic_or", 3, 0x00, D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_ATOMIC_XOR, "atomic_xor", 3, 0x00, D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_ATOMIC_CMP_STORE, "atomic_cmp_store", 4, 0x00,
      D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_ATOMIC_IADD, "atomic_iadd", 3, 0x00, D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_ATOMIC_IMAX, "atomic_imax", 3, 0x00, D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_ATOMIC_IMIN, "atomic_imin", 3, 0x00, D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_ATOMIC_UMAX, "atomic_umax", 3, 0x00, D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_ATOMIC_UMIN, "atomic_umin", 3, 0x00, D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_IMM_ATOMIC_ALLOC, "imm_atomic_alloc", 2, 0x00,
      D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_IMM_ATOMIC_CONSUME, "imm_atomic_consume", 2, 0x00,
      D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_IMM_ATOMIC_IADD, "imm_atomic_iadd", 4, 0x00,
      D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_IMM_ATOMIC_AND, "imm_atomic_and", 4, 0x00,
      D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_IMM_ATOMIC_OR, "imm_atomic_or", 4, 0x00,
      D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_IMM_ATOMIC_XOR, "imm_atomic_xor", 4, 0x00,
      D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_IMM_ATOMIC_EXCH, "imm_atomic_exch", 4, 0x00,
      D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_IMM_ATOMIC_CMP_EXCH, "imm_atomic_cmp_exch", 5, 0x00,
      D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_IMM_ATOMIC_IMAX, "imm_atomic_imax", 4, 0x00,
      D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_IMM_ATOMIC_IMIN, "imm_atomic_imin", 4, 0x00,
      D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_IMM_ATOMIC_UMAX, "imm_atomic_umax", 4, 0x00,
      D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_IMM_ATOMIC_UMIN, "imm_atomic_umin", 4, 0x00,
      D3D11_SB_ATOMIC_OP);
  SET(D3D11_SB_OPCODE_SYNC, "sync", 0, 0x00, D3D10_SB_FLOW_OP);
  SET(D3D11_SB_OPCODE_EVAL_SNAPPED, "eval_snapped", 3, 0x02, D3D10_SB_FLOAT_OP);
  SET(D3D11_SB_OPCODE_EVAL_SAMPLE_INDEX, "eval_sample_index", 3, 0x02,
      D3D10_SB_FLOAT_OP);
  SET(D3D11_SB_OPCODE_EVAL_CENTROID, "eval_centroid", 2, 0x02,
      D3D10_SB_FLOAT_OP);

  SET(D3D11_SB_OPCODE_DCL_GS_INSTANCE_COUNT, "dcl_gsinstances", 0, 0x00,
      D3D10_SB_DCL_OP);

  SET(D3D11_SB_OPCODE_DADD, "dadd", 3, 0x06, D3D11_SB_DOUBLE_OP);
  SET(D3D11_SB_OPCODE_DMAX, "dmax", 3, 0x06, D3D11_SB_DOUBLE_OP);
  SET(D3D11_SB_OPCODE_DMIN, "dmin", 3, 0x06, D3D11_SB_DOUBLE_OP);
  SET(D3D11_SB_OPCODE_DMUL, "dmul", 3, 0x06, D3D11_SB_DOUBLE_OP);
  SET(D3D11_SB_OPCODE_DEQ, "deq", 3, 0x00, D3D11_SB_DOUBLE_OP);
  SET(D3D11_SB_OPCODE_DGE, "dge", 3, 0x00, D3D11_SB_DOUBLE_OP);
  SET(D3D11_SB_OPCODE_DLT, "dlt", 3, 0x00, D3D11_SB_DOUBLE_OP);
  SET(D3D11_SB_OPCODE_DNE, "dne", 3, 0x00, D3D11_SB_DOUBLE_OP);
  SET(D3D11_SB_OPCODE_DMOV, "dmov", 2, 0x02, D3D11_SB_DOUBLE_OP);
  SET(D3D11_SB_OPCODE_DMOVC, "dmovc", 4, 0x0c, D3D11_SB_DOUBLE_OP);
  SET(D3D11_SB_OPCODE_DTOF, "dtof", 2, 0x02, D3D11_SB_DOUBLE_TO_FLOAT_OP);
  SET(D3D11_SB_OPCODE_FTOD, "ftod", 2, 0x00, D3D11_SB_FLOAT_TO_DOUBLE_OP);

  SET(D3D11_SB_OPCODE_ABORT, "abort", 0, 0x00, D3D11_SB_DEBUG_OP);
  SET(D3D11_SB_OPCODE_DEBUG_BREAK, "debug_break", 0, 0x00, D3D11_SB_DEBUG_OP);

  SET(D3D11_1_SB_OPCODE_DDIV, "ddiv", 3, 0x06, D3D11_SB_DOUBLE_OP);
  SET(D3D11_1_SB_OPCODE_DFMA, "dfma", 4, 0x0e, D3D11_SB_DOUBLE_OP);
  SET(D3D11_1_SB_OPCODE_DRCP, "drcp", 2, 0x02, D3D11_SB_DOUBLE_OP);

  SET(D3D11_1_SB_OPCODE_MSAD, "msad", 4, 0x0e, D3D10_SB_UINT_OP);

  SET(D3D11_1_SB_OPCODE_DTOI, "dtoi", 2, 0x00, D3D11_SB_DOUBLE_OP);
  SET(D3D11_1_SB_OPCODE_DTOU, "dtou", 2, 0x00, D3D11_SB_DOUBLE_OP);
  SET(D3D11_1_SB_OPCODE_ITOD, "itod", 2, 0x00, D3D10_SB_INT_OP);
  SET(D3D11_1_SB_OPCODE_UTOD, "utod", 2, 0x00, D3D10_SB_UINT_OP);

  SET(D3DWDDM1_3_SB_OPCODE_GATHER4_FEEDBACK, "gather4_s", 5, 0x00,
      D3D10_SB_TEX_OP);
  SET(D3DWDDM1_3_SB_OPCODE_GATHER4_C_FEEDBACK, "gather4_c_s", 6, 0x00,
      D3D10_SB_TEX_OP);
  SET(D3DWDDM1_3_SB_OPCODE_GATHER4_PO_FEEDBACK, "gather4_po_s", 6, 0x00,
      D3D10_SB_TEX_OP);
  SET(D3DWDDM1_3_SB_OPCODE_GATHER4_PO_C_FEEDBACK, "gather4_po_c_s", 7, 0x00,
      D3D10_SB_TEX_OP);
  SET(D3DWDDM1_3_SB_OPCODE_LD_FEEDBACK, "ld_s", 4, 0x00, D3D10_SB_TEX_OP);
  SET(D3DWDDM1_3_SB_OPCODE_LD_MS_FEEDBACK, "ldms_s", 5, 0x00, D3D10_SB_TEX_OP);
  SET(D3DWDDM1_3_SB_OPCODE_LD_UAV_TYPED_FEEDBACK, "ld_uav_typed_s", 4, 0x00,
      D3D11_SB_MEM_OP);
  SET(D3DWDDM1_3_SB_OPCODE_LD_RAW_FEEDBACK, "ld_raw_s", 4, 0x00,
      D3D11_SB_MEM_OP);
  SET(D3DWDDM1_3_SB_OPCODE_LD_STRUCTURED_FEEDBACK, "ld_structured_s", 5, 0x00,
      D3D11_SB_MEM_OP);
  SET(D3DWDDM1_3_SB_OPCODE_SAMPLE_L_FEEDBACK, "sample_l_s", 6, 0x00,
      D3D10_SB_TEX_OP);
  SET(D3DWDDM1_3_SB_OPCODE_SAMPLE_C_LZ_FEEDBACK, "sample_c_lz_s", 6, 0x00,
      D3D10_SB_TEX_OP);
  SET(D3DWDDM1_3_SB_OPCODE_SAMPLE_CLAMP_FEEDBACK, "sample_cl_s", 6, 0x00,
      D3D10_SB_TEX_OP);
  SET(D3DWDDM1_3_SB_OPCODE_SAMPLE_B_CLAMP_FEEDBACK, "sample_b_cl_s", 7, 0x00,
      D3D10_SB_TEX_OP);
  SET(D3DWDDM1_3_SB_OPCODE_SAMPLE_D_CLAMP_FEEDBACK, "sample_d_cl_s", 8, 0x00,
      D3D10_SB_TEX_OP);
  SET(D3DWDDM1_3_SB_OPCODE_SAMPLE_C_CLAMP_FEEDBACK, "sample_c_cl_s", 7, 0x00,
      D3D10_SB_TEX_OP);
  SET(D3DWDDM1_3_SB_OPCODE_CHECK_ACCESS_FULLY_MAPPED,
      "check_access_fully_mapped", 2, 0x00, D3D10_SB_TEX_OP);
  // clang-format on
}

struct InstructionModifier {
  uint32_t preciseMask{0};
  uint32_t saturate{0};
};

// Whether an op carries a precise modifier attribute.
enum class HasPreciseAttr { No, Yes };

struct ExtendedInstructionSampleOffset {
  int32_t u;
  int32_t v;
  int32_t w;
};

struct ExtendedInstructionResourceDim {
  uint32_t dim;
  std::optional<uint32_t> stride;
};

struct ExtendedInstructionResourceReturnType {
  uint32_t x;
  uint32_t y;
  uint32_t z;
  uint32_t w;
};

struct ExtendedInstruction {
  std::optional<ExtendedInstructionSampleOffset> sampleOffset;
  std::optional<ExtendedInstructionResourceDim> resourceDim;
  std::optional<ExtendedInstructionResourceReturnType> resourceReturnType;
};

struct OperandModifier {
  uint32_t modifier{0};
  uint32_t minPrecision{0};
  uint32_t nonUniform{0};
};

enum class OperandComponentsKind {
  None,
  Mask,
  Swizzle,
  One,
};

struct OperandComponents {
  unsigned num;
  OperandComponentsKind kind;
  union {
    uint32_t mask;
    uint32_t swizzle[4];
    uint32_t one;
  };
};

static dxsa::ComponentMask decodeComponentMask(uint32_t rawComponentMask) {
  auto componentMask = static_cast<dxsa::ComponentMask>(0);
  if (rawComponentMask & D3D10_SB_OPERAND_4_COMPONENT_MASK_X)
    componentMask |= dxsa::ComponentMask::x;
  if (rawComponentMask & D3D10_SB_OPERAND_4_COMPONENT_MASK_Y)
    componentMask |= dxsa::ComponentMask::y;
  if (rawComponentMask & D3D10_SB_OPERAND_4_COMPONENT_MASK_Z)
    componentMask |= dxsa::ComponentMask::z;
  if (rawComponentMask & D3D10_SB_OPERAND_4_COMPONENT_MASK_W)
    componentMask |= dxsa::ComponentMask::w;
  return componentMask;
}

class DXBuilder {
public:
  explicit DXBuilder(MLIRContext *context)
      : context(context), builder(context) {}

  using Index = mlir::Value;
  using Operand = mlir::Value;
  using Instruction = mlir::Operation *;
  using Module = dxsa::ModuleOp;

  Module createModule(dxsa::ProgramTypeAttr programType,
                      std::optional<uint32_t> majorVersion,
                      std::optional<uint32_t> minorVersion, Location loc) {
    auto major =
        majorVersion ? builder.getI32IntegerAttr(*majorVersion) : IntegerAttr();
    auto minor =
        minorVersion ? builder.getI32IntegerAttr(*minorVersion) : IntegerAttr();
    auto module = Module::create(builder, loc, programType, major, minor);
    builder.createBlock(&module.getBody());
    return module;
  }

  Index buildIndexImm32(int32_t imm, FileLineColLoc loc) {
    Operation *op =
        dxsa::IndexImm::create(builder, loc, builder.getType<dxsa::IndexType>(),
                               builder.getI32IntegerAttr(imm));
    return op->getResults()[0];
  }

  Index buildIndexImm64(int64_t imm, FileLineColLoc loc) {
    Operation *op =
        dxsa::IndexImm::create(builder, loc, builder.getType<dxsa::IndexType>(),
                               builder.getI64IntegerAttr(imm));
    return op->getResults()[0];
  }

  Index buildIndexRelative(Operand operand, FileLineColLoc loc) {
    Operation *op = dxsa::IndexRel::create(
        builder, loc, builder.getType<dxsa::IndexType>(), operand);
    return op->getResults()[0];
  }

  Index buildIndexImm32PlusRelative(int32_t imm, Operand operand,
                                    FileLineColLoc loc) {
    Operation *op = dxsa::IndexRelImm::create(
        builder, loc, builder.getType<dxsa::IndexType>(), operand,
        builder.getStringAttr("add"), builder.getI32IntegerAttr(imm));
    return op->getResults()[0];
  }

  Operand buildOperandImm32(ArrayRef<int32_t> values, FileLineColLoc loc) {
    Operation *op = dxsa::OperandImm::create(
        builder, loc, builder.getType<dxsa::LegacyOperandType>(),
        builder.getI32VectorAttr(values));
    return op->getResults()[0];
  }

  Operand buildOperandImm64(ArrayRef<int64_t> values, FileLineColLoc loc) {
    Operation *op = dxsa::OperandImm::create(
        builder, loc, builder.getType<dxsa::LegacyOperandType>(),
        builder.getI64VectorAttr(values));
    return op->getResults()[0];
  }

  Operand buildOperand(uint32_t opType, const OperandComponents &components,
                       ArrayRef<Index> indices,
                       const std::optional<OperandModifier> &modifier,
                       FileLineColLoc loc) {
    NamedAttrList attrs;
    attrs.append("type", builder.getI32IntegerAttr(opType));
    if (modifier) {
      const OperandModifier &mod = modifier.value();
      if (uint32_t modifier = mod.modifier) {
        attrs.append("modifier", builder.getI32IntegerAttr(modifier));
      }
      if (uint32_t minPrecision = mod.minPrecision) {
        attrs.append("min_precision", builder.getI32IntegerAttr(minPrecision));
      }
      if (uint32_t nonUniform = mod.nonUniform) {
        attrs.append("non_uniform", builder.getI32IntegerAttr(nonUniform));
      }
    }
    attrs.append("num_components", builder.getI32IntegerAttr(components.num));
    switch (components.kind) {
    case OperandComponentsKind::Mask: {
      attrs.append("mask", builder.getI32IntegerAttr(components.mask));
      break;
    }
    case OperandComponentsKind::Swizzle: {
      SmallVector<int32_t, 4> values;
      for (uint32_t i = 0; i < components.num; ++i) {
        values.push_back(components.swizzle[i]);
      }
      attrs.append("swizzle", builder.getI32VectorAttr(values));
      break;
    }
    case OperandComponentsKind::One: {
      attrs.append("one", builder.getI32IntegerAttr(components.one));
      break;
    }
    case OperandComponentsKind::None:
      break;
    }
    Operation *op = dxsa::Operand::create(
        builder, loc, builder.getType<dxsa::LegacyOperandType>(), indices,
        attrs);
    return op->getResults()[0];
  }

  size_t getNumOps() {
    return builder.getInsertionBlock()->getOperations().size();
  }

  void rewindOpsTo(size_t numOps) {
    auto *block = builder.getInsertionBlock();
    while (block->getOperations().size() > numOps)
      block->back().erase(); // reverse order: use-def stays valid
    builder.setInsertionPointToEnd(block);
  }

  Instruction buildInstruction(StringRef name, ArrayRef<Operand> operands,
                               const InstructionModifier &modifier,
                               FileLineColLoc loc) {
    return dxsa::Instruction::create(builder, loc, operands,
                                     builder.getStringAttr(name));
  }

  Instruction buildUnknown(ArrayRef<uint32_t> tokens, Location loc) {
    auto signedTokens = llvm::map_to_vector(
        tokens, [](uint32_t token) { return static_cast<int32_t>(token); });
    return dxsa::Unknown::create(
        builder, loc,
        DenseI32ArrayAttr::get(builder.getContext(), signedTokens));
  }

  Instruction buildDclGlobalFlags(dxsa::GlobalFlags flags, Location loc) {
    auto flagsAttr = dxsa::GlobalFlagsAttr::get(builder.getContext(), flags);
    return dxsa::DclGlobalFlags::create(builder, loc, flagsAttr);
  }

  Instruction buildSync(dxsa::SyncFlags flags, Location loc) {
    auto flagsAttr = dxsa::SyncFlagsAttr::get(builder.getContext(), flags);
    return dxsa::Sync::create(builder, loc, flagsAttr);
  }

  Instruction buildDclTemps(uint32_t count, Location loc) {
    return dxsa::DclTemps::create(builder, loc,
                                  builder.getI32IntegerAttr(count));
  }

  Instruction buildDclInputControlPointCount(uint32_t count, Location loc) {
    return dxsa::DclInputControlPointCount::create(
        builder, loc, builder.getI32IntegerAttr(count));
  }

  Instruction buildDclOutputControlPointCount(uint32_t count, Location loc) {
    return dxsa::DclOutputControlPointCount::create(
        builder, loc, builder.getI32IntegerAttr(count));
  }

  Instruction buildDclTessellatorDomain(dxsa::TessellatorDomain domain,
                                        Location loc) {
    auto domainAttr =
        dxsa::TessellatorDomainAttr::get(builder.getContext(), domain);
    return dxsa::DclTessellatorDomain::create(builder, loc, domainAttr);
  }

  Instruction buildDclTessellatorOutputPrimitive(
      dxsa::TessellatorOutputPrimitiveType outputPrimitiveType, Location loc) {
    auto outputPrimitiveTypeAttr =
        dxsa::TessellatorOutputPrimitiveTypeAttr::get(builder.getContext(),
                                                      outputPrimitiveType);
    return dxsa::DclTessellatorOutputPrimitive::create(builder, loc,
                                                       outputPrimitiveTypeAttr);
  }

  Instruction
  buildDclOutputTopology(dxsa::OutputPrimitiveTopology outputTopology,
                         Location loc) {
    auto outputTopologyAttr = dxsa::OutputPrimitiveTopologyAttr::get(
        builder.getContext(), outputTopology);
    return dxsa::DclOutputTopology::create(builder, loc, outputTopologyAttr);
  }

  Instruction buildDclTessellatorPartitioning(
      dxsa::TessellatorPartitioningMode partitioningMode, Location loc) {
    auto partitioningModeAttr = dxsa::TessellatorPartitioningModeAttr::get(
        builder.getContext(), partitioningMode);
    return dxsa::DclTessellatorPartitioning::create(builder, loc,
                                                    partitioningModeAttr);
  }

  Instruction buildDclInputPrimitive(dxsa::InputPrimitive inputPrimitive,
                                     Location loc) {
    auto inputPrimitiveAttr =
        dxsa::InputPrimitiveAttr::get(builder.getContext(), inputPrimitive);
    return dxsa::DclInputPrimitive::create(builder, loc, inputPrimitiveAttr);
  }

  Instruction buildDclGsInstanceCount(uint32_t count, Location loc) {
    return dxsa::DclGsInstanceCount::create(builder, loc,
                                            builder.getI32IntegerAttr(count));
  }

  Instruction buildDclMaxOutputVertexCount(uint32_t count, Location loc) {
    return dxsa::DclMaxOutputVertexCount::create(builder, loc, count);
  }

  template <typename OpT>
  Instruction buildGsStreamIndexOp(uint32_t index, Location loc) {
    return OpT::create(builder, loc, builder.getI32IntegerAttr(index));
  }

  Instruction buildDclInputPs(dxsa::InterpolationMode interpolationMode,
                              dxsa::DstOperandAttr operand, Location loc) {
    auto interpolationModeAttr = dxsa::InterpolationModeAttr::get(
        builder.getContext(), interpolationMode);
    return dxsa::DclInputPs::create(builder, loc, interpolationModeAttr,
                                    operand);
  }

  Instruction buildDclInputPsSiv(dxsa::InterpolationMode interpolationMode,
                                 dxsa::DstOperandAttr operand,
                                 dxsa::SystemValueName systemValueName,
                                 Location loc) {
    auto interpolationModeAttr = dxsa::InterpolationModeAttr::get(
        builder.getContext(), interpolationMode);
    auto systemValueNameAttr =
        dxsa::SystemValueNameAttr::get(builder.getContext(), systemValueName);
    return dxsa::DclInputPsSiv::create(builder, loc, interpolationModeAttr,
                                       operand, systemValueNameAttr);
  }

  Instruction buildDclInputPsSgv(dxsa::DstOperandAttr operand,
                                 dxsa::SystemValueName systemValueName,
                                 Location loc) {
    auto systemValueNameAttr =
        dxsa::SystemValueNameAttr::get(builder.getContext(), systemValueName);
    return dxsa::DclInputPsSgv::create(builder, loc, operand,
                                       systemValueNameAttr);
  }

  dxsa::DstOperandAttr
  buildDstOperandAttr(dxsa::OperandType operandType,
                      const OperandComponents &components,
                      ArrayRef<dxsa::IndexAttr> indexEntries,
                      std::optional<OperandModifier> opModifier) {
    auto componentsValue = components.num == 0 ? dxsa::OperandComponents::none
                           : components.num == 1
                               ? dxsa::OperandComponents::scalar
                               : dxsa::OperandComponents::vector;
    auto componentsAttr =
        dxsa::OperandComponentsAttr::get(context, componentsValue);

    dxsa::ComponentMaskAttr maskAttr;
    if (components.kind == OperandComponentsKind::Mask)
      maskAttr = dxsa::ComponentMaskAttr::get(
          context, decodeComponentMask(components.mask));

    dxsa::OperandIndexAttr indexAttr;
    if (!indexEntries.empty())
      indexAttr = dxsa::OperandIndexAttr::get(context, indexEntries);

    dxsa::OperandMinPrecisionAttr minPrecisionAttr;
    if (opModifier && opModifier->minPrecision != 0) {
      if (auto p = dxsa::symbolizeOperandMinPrecision(opModifier->minPrecision))
        minPrecisionAttr = dxsa::OperandMinPrecisionAttr::get(context, *p);
    }

    return dxsa::DstOperandAttr::get(context, operandType, indexAttr, maskAttr,
                                     componentsAttr, minPrecisionAttr);
  }

  dxsa::SrcOperandAttr
  buildSrcOperandAttr(dxsa::OperandType operandType,
                      const OperandComponents &components,
                      ArrayRef<dxsa::IndexAttr> indexEntries,
                      std::optional<OperandModifier> opModifier,
                      ArrayRef<int32_t> values, ArrayRef<int64_t> values64) {
    auto componentsValue = components.num == 0 ? dxsa::OperandComponents::none
                           : components.num == 1
                               ? dxsa::OperandComponents::scalar
                               : dxsa::OperandComponents::vector;
    auto componentsAttr =
        dxsa::OperandComponentsAttr::get(context, componentsValue);

    dxsa::SwizzleAttr swizzleAttr;
    if (components.kind == OperandComponentsKind::Swizzle) {
      SmallVector<unsigned, 4> swizzleComponents;
      for (unsigned int i : components.swizzle)
        swizzleComponents.push_back(i);
      swizzleAttr = dxsa::SwizzleAttr::get(context, swizzleComponents);
    } else if (components.kind == OperandComponentsKind::One) {
      swizzleAttr = dxsa::SwizzleAttr::get(
          context, ArrayRef<unsigned>{static_cast<unsigned>(components.one)});
    }

    dxsa::OperandIndexAttr indexAttr;
    if (!indexEntries.empty())
      indexAttr = dxsa::OperandIndexAttr::get(context, indexEntries);

    dxsa::OperandModifierAttr modifierAttr;
    dxsa::OperandMinPrecisionAttr minPrecisionAttr;
    UnitAttr nonUniformAttr;
    if (opModifier) {
      if (opModifier->modifier != 0) {
        if (auto m = dxsa::symbolizeOperandModifier(opModifier->modifier))
          modifierAttr = dxsa::OperandModifierAttr::get(context, *m);
      }
      if (opModifier->minPrecision != 0) {
        if (auto p =
                dxsa::symbolizeOperandMinPrecision(opModifier->minPrecision))
          minPrecisionAttr = dxsa::OperandMinPrecisionAttr::get(context, *p);
      }
      if (opModifier->nonUniform != 0)
        nonUniformAttr = UnitAttr::get(context);
    }

    auto valuesAttr = values.empty() ? DenseI32ArrayAttr()
                                     : DenseI32ArrayAttr::get(context, values);
    auto values64Attr = values64.empty()
                            ? DenseI64ArrayAttr()
                            : DenseI64ArrayAttr::get(context, values64);

    return dxsa::SrcOperandAttr::get(
        context, operandType, indexAttr, componentsAttr, minPrecisionAttr,
        nonUniformAttr, swizzleAttr, modifierAttr, valuesAttr, values64Attr);
  }

  dxsa::IndexAttr buildOperandIndexImm32(int32_t imm) {
    return dxsa::IndexAttr::get(context, builder.getI32IntegerAttr(imm),
                                dxsa::SrcOperandAttr());
  }

  dxsa::IndexAttr buildOperandIndexImm64(int64_t imm) {
    return dxsa::IndexAttr::get(context, builder.getI64IntegerAttr(imm),
                                dxsa::SrcOperandAttr());
  }

  dxsa::IndexAttr buildOperandIndexRelative(dxsa::SrcOperandAttr relative) {
    return dxsa::IndexAttr::get(context, IntegerAttr(), relative);
  }

  dxsa::IndexAttr
  buildOperandIndexImm32PlusRelative(int32_t imm,
                                     dxsa::SrcOperandAttr relative) {
    return dxsa::IndexAttr::get(context, builder.getI32IntegerAttr(imm),
                                relative);
  }

  dxsa::IndexAttr
  buildOperandIndexImm64PlusRelative(int64_t imm,
                                     dxsa::SrcOperandAttr relative) {
    return dxsa::IndexAttr::get(context, builder.getI64IntegerAttr(imm),
                                relative);
  }

  dxsa::ComponentMaskAttr buildPreciseAttr(uint32_t preciseMask) {
    if (!preciseMask)
      return dxsa::ComponentMaskAttr();
    return dxsa::ComponentMaskAttr::get(
        context, static_cast<dxsa::ComponentMask>(preciseMask));
  }

  template <typename OpT, HasPreciseAttr HasPrecise, std::size_t NumDstOperands,
            std::size_t NumSrcOperands>
  Instruction
  buildOp(uint32_t preciseMask, Location loc,
          const std::array<dxsa::DstOperandAttr, NumDstOperands> &dsts,
          const std::array<dxsa::SrcOperandAttr, NumSrcOperands> &srcs) {
    return std::apply(
        [&](auto... dstOperands) {
          return std::apply(
              [&](auto... srcOperands) -> Instruction {
                if constexpr (HasPrecise == HasPreciseAttr::Yes)
                  return OpT::create(builder, loc, dstOperands...,
                                     srcOperands...,
                                     buildPreciseAttr(preciseMask));
                else
                  return OpT::create(builder, loc, dstOperands...,
                                     srcOperands...);
              },
              srcs);
        },
        dsts);
  }

  Instruction buildDclInput(dxsa::DstOperandAttr operand, Location loc) {
    return dxsa::DclInput::create(builder, loc, operand);
  }

  Instruction buildDclOutput(dxsa::DstOperandAttr operand, Location loc) {
    return dxsa::DclOutput::create(builder, loc, operand);
  }

  Instruction buildDclIndexRange(dxsa::DstOperandAttr operand, uint32_t count,
                                 Location loc) {
    return dxsa::DclIndexRange::create(builder, loc, operand,
                                       builder.getI32IntegerAttr(count));
  }

  Instruction buildDclInputSgv(dxsa::DstOperandAttr operand,
                               dxsa::SystemValueName name, Location loc) {
    auto nameAttr = dxsa::SystemValueNameAttr::get(builder.getContext(), name);
    return dxsa::DclInputSgv::create(builder, loc, operand, nameAttr);
  }

  Instruction buildDclInputSiv(dxsa::DstOperandAttr operand,
                               dxsa::SystemValueName name, Location loc) {
    auto nameAttr = dxsa::SystemValueNameAttr::get(builder.getContext(), name);
    return dxsa::DclInputSiv::create(builder, loc, operand, nameAttr);
  }

  Instruction buildDclOutputSgv(dxsa::DstOperandAttr operand,
                                dxsa::SystemValueName name, Location loc) {
    auto nameAttr = dxsa::SystemValueNameAttr::get(builder.getContext(), name);
    return dxsa::DclOutputSgv::create(builder, loc, operand, nameAttr);
  }

  Instruction buildDclOutputSiv(dxsa::DstOperandAttr operand,
                                dxsa::SystemValueName name, Location loc) {
    auto nameAttr = dxsa::SystemValueNameAttr::get(builder.getContext(), name);
    return dxsa::DclOutputSiv::create(builder, loc, operand, nameAttr);
  }

  Instruction buildDclHsMaxTessFactor(float maxTessFactor, Location loc) {
    return dxsa::DclHsMaxTessFactor::create(
        builder, loc, builder.getF32FloatAttr(maxTessFactor));
  }

  Instruction buildDclHsJoinPhaseInstanceCount(uint32_t count, Location loc) {
    return dxsa::DclHsJoinPhaseInstanceCount::create(
        builder, loc, builder.getUI32IntegerAttr(count));
  }

  Instruction buildDclHsForkPhaseInstanceCount(uint32_t count, Location loc) {
    return dxsa::DclHsForkPhaseInstanceCount::create(
        builder, loc, builder.getUI32IntegerAttr(count));
  }

  Instruction buildDclTgsmRaw(dxsa::DstOperandAttr operand, uint32_t byteCount,
                              Location loc) {
    return dxsa::DclTgsmRaw::create(builder, loc, operand,
                                    builder.getI32IntegerAttr(byteCount));
  }

  Instruction buildDclTgsmStructured(dxsa::DstOperandAttr operand,
                                     uint32_t structByteStride,
                                     uint32_t structCount, Location loc) {
    return dxsa::DclTgsmStructured::create(
        builder, loc, operand, builder.getI32IntegerAttr(structByteStride),
        builder.getI32IntegerAttr(structCount));
  }

  Instruction buildDclConstantBuffer(
      uint32_t id, uint32_t size, std::optional<uint32_t> lbound,
      std::optional<uint32_t> ubound, std::optional<uint32_t> space,
      dxsa::ConstantBufferAccessPattern accessPattern, Location loc) {
    auto optionalToAttr = [&](std::optional<uint32_t> v) -> IntegerAttr {
      return v ? builder.getI32IntegerAttr(*v) : IntegerAttr();
    };
    return dxsa::DclConstantBuffer::create(
        builder, loc, id, size, optionalToAttr(lbound), optionalToAttr(ubound),
        optionalToAttr(space), accessPattern);
  }

  Instruction buildDclImmediateConstantBuffer(ArrayRef<float> values,
                                              Location loc) {
    return dxsa::DclImmediateConstantBuffer::create(builder, loc, values);
  }

  Instruction buildDclSampler(uint32_t id, std::optional<uint32_t> lbound,
                              std::optional<uint32_t> ubound,
                              std::optional<uint32_t> space,
                              dxsa::SamplerMode mode, Location loc) {
    auto optionalToAttr = [&](std::optional<uint32_t> v) -> IntegerAttr {
      return v ? builder.getI32IntegerAttr(*v) : IntegerAttr();
    };
    return dxsa::DclSampler::create(
        builder, loc, id, mode, optionalToAttr(lbound), optionalToAttr(ubound),
        optionalToAttr(space));
  }

  Instruction buildDclResource(
      uint32_t id, dxsa::ResourceDimension dim, dxsa::ResourceReturnType x,
      dxsa::ResourceReturnType y, dxsa::ResourceReturnType z,
      dxsa::ResourceReturnType w, std::optional<uint32_t> sampleCount,
      std::optional<uint32_t> lbound, std::optional<uint32_t> ubound,
      std::optional<uint32_t> space, Location loc) {
    auto toAttr = [&](std::optional<uint32_t> v) -> IntegerAttr {
      return v ? builder.getI32IntegerAttr(*v) : IntegerAttr();
    };
    return dxsa::DclResource::create(builder, loc, id, dim, x, y, z, w,
                                     toAttr(sampleCount), toAttr(lbound),
                                     toAttr(ubound), toAttr(space));
  }

  Instruction buildDclResourceStructured(uint32_t id, uint32_t structByteStride,
                                         std::optional<uint32_t> lbound,
                                         std::optional<uint32_t> ubound,
                                         std::optional<uint32_t> space,
                                         Location loc) {
    auto toAttr = [&](std::optional<uint32_t> v) -> IntegerAttr {
      return v ? builder.getI32IntegerAttr(*v) : IntegerAttr();
    };
    return dxsa::DclResourceStructured::create(builder, loc, id,
                                               structByteStride, toAttr(lbound),
                                               toAttr(ubound), toAttr(space));
  }

  Instruction buildDclResourceRaw(uint32_t id, std::optional<uint32_t> lbound,
                                  std::optional<uint32_t> ubound,
                                  std::optional<uint32_t> space, Location loc) {
    auto toAttr = [&](std::optional<uint32_t> v) -> IntegerAttr {
      return v ? builder.getI32IntegerAttr(*v) : IntegerAttr();
    };
    return dxsa::DclResourceRaw::create(builder, loc, id, toAttr(lbound),
                                        toAttr(ubound), toAttr(space));
  }

  dxsa::UAVFlagsAttr buildUavFlagsAttr(std::optional<dxsa::UAVFlags> flags) {
    return flags ? dxsa::UAVFlagsAttr::get(context, *flags)
                 : dxsa::UAVFlagsAttr();
  }

  Instruction buildDclUavTyped(
      uint32_t id, dxsa::ResourceDimension dim, dxsa::ResourceReturnType x,
      dxsa::ResourceReturnType y, dxsa::ResourceReturnType z,
      dxsa::ResourceReturnType w, std::optional<dxsa::UAVFlags> flags,
      std::optional<uint32_t> lbound, std::optional<uint32_t> ubound,
      std::optional<uint32_t> space, Location loc) {
    auto toAttr = [&](std::optional<uint32_t> v) -> IntegerAttr {
      return v ? builder.getI32IntegerAttr(*v) : IntegerAttr();
    };
    return dxsa::DclUavTyped::create(builder, loc, id, dim, x, y, z, w,
                                     buildUavFlagsAttr(flags), toAttr(lbound),
                                     toAttr(ubound), toAttr(space));
  }

  Instruction buildDclUavRaw(uint32_t id, std::optional<dxsa::UAVFlags> flags,
                             std::optional<uint32_t> lbound,
                             std::optional<uint32_t> ubound,
                             std::optional<uint32_t> space, Location loc) {
    auto toAttr = [&](std::optional<uint32_t> v) -> IntegerAttr {
      return v ? builder.getI32IntegerAttr(*v) : IntegerAttr();
    };
    return dxsa::DclUavRaw::create(builder, loc, id, buildUavFlagsAttr(flags),
                                   toAttr(lbound), toAttr(ubound),
                                   toAttr(space));
  }

  Instruction buildDclUavStructured(uint32_t id, uint32_t structByteStride,
                                    std::optional<dxsa::UAVFlags> flags,
                                    std::optional<uint32_t> lbound,
                                    std::optional<uint32_t> ubound,
                                    std::optional<uint32_t> space,
                                    Location loc) {
    auto toAttr = [&](std::optional<uint32_t> v) -> IntegerAttr {
      return v ? builder.getI32IntegerAttr(*v) : IntegerAttr();
    };
    return dxsa::DclUavStructured::create(
        builder, loc, id, structByteStride, buildUavFlagsAttr(flags),
        toAttr(lbound), toAttr(ubound), toAttr(space));
  }

  Instruction buildDclIndexableTemp(uint32_t id, uint32_t size,
                                    uint32_t numComponents, Location loc) {
    return dxsa::DclIndexableTemp::create(
        builder, loc, builder.getI32IntegerAttr(id),
        builder.getI32IntegerAttr(size),
        builder.getI32IntegerAttr(numComponents));
  }

  Instruction buildDclThreadGroup(uint32_t x, uint32_t y, uint32_t z,
                                  Location loc) {
    return dxsa::DclThreadGroup::create(
        builder, loc, builder.getI32IntegerAttr(x),
        builder.getI32IntegerAttr(y), builder.getI32IntegerAttr(z));
  }

  dxsa::SampleOffsetAttr
  buildSampleOffsetAttr(const ExtendedInstructionSampleOffset &sampleOffset) {
    return dxsa::SampleOffsetAttr::get(context, sampleOffset.u, sampleOffset.v,
                                       sampleOffset.w);
  }

  dxsa::SampleClampFeedbackAttr
  buildSampleClampFeedbackAttr(dxsa::SrcOperandAttr clamp,
                               dxsa::DstOperandAttr feedback) {
    return dxsa::SampleClampFeedbackAttr::get(context, clamp, feedback);
  }

  Instruction buildSample(dxsa::DstOperandAttr dst,
                          dxsa::SrcOperandAttr srcAddress,
                          dxsa::SrcOperandAttr srcResource,
                          dxsa::SrcOperandAttr srcSampler,
                          dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::Sample::create(builder, loc, dst, srcAddress, srcResource,
                                srcSampler, offset);
  }

  Instruction buildSampleClampFeedback(
      dxsa::DstOperandAttr dst, dxsa::SrcOperandAttr srcAddress,
      dxsa::SrcOperandAttr srcResource, dxsa::SrcOperandAttr srcSampler,
      dxsa::SampleClampFeedbackAttr clampFeedback,
      dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::SampleClampFeedback::create(builder, loc, dst, srcAddress,
                                             srcResource, srcSampler,
                                             clampFeedback, offset);
  }

  Instruction buildSampleB(dxsa::DstOperandAttr dst,
                           dxsa::SrcOperandAttr srcAddress,
                           dxsa::SrcOperandAttr srcResource,
                           dxsa::SrcOperandAttr srcSampler,
                           dxsa::SrcOperandAttr srcLodBias,
                           dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::SampleB::create(builder, loc, dst, srcAddress, srcResource,
                                 srcSampler, srcLodBias, offset);
  }

  Instruction buildSampleBClampFeedback(
      dxsa::DstOperandAttr dst, dxsa::SrcOperandAttr srcAddress,
      dxsa::SrcOperandAttr srcResource, dxsa::SrcOperandAttr srcSampler,
      dxsa::SrcOperandAttr srcLodBias,
      dxsa::SampleClampFeedbackAttr clampFeedback,
      dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::SampleBClampFeedback::create(
        builder, loc, dst, srcAddress, srcResource, srcSampler, srcLodBias,
        clampFeedback, offset);
  }

  Instruction buildSampleD(dxsa::DstOperandAttr dst,
                           dxsa::SrcOperandAttr srcAddress,
                           dxsa::SrcOperandAttr srcResource,
                           dxsa::SrcOperandAttr srcSampler,
                           dxsa::SrcOperandAttr srcXDerivatives,
                           dxsa::SrcOperandAttr srcYDerivatives,
                           dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::SampleD::create(builder, loc, dst, srcAddress, srcResource,
                                 srcSampler, srcXDerivatives, srcYDerivatives,
                                 offset);
  }

  Instruction buildSampleDClampFeedback(
      dxsa::DstOperandAttr dst, dxsa::SrcOperandAttr srcAddress,
      dxsa::SrcOperandAttr srcResource, dxsa::SrcOperandAttr srcSampler,
      dxsa::SrcOperandAttr srcXDerivatives,
      dxsa::SrcOperandAttr srcYDerivatives,
      dxsa::SampleClampFeedbackAttr clampFeedback,
      dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::SampleDClampFeedback::create(
        builder, loc, dst, srcAddress, srcResource, srcSampler, srcXDerivatives,
        srcYDerivatives, clampFeedback, offset);
  }

  Instruction buildSampleL(dxsa::DstOperandAttr dst,
                           dxsa::SrcOperandAttr srcAddress,
                           dxsa::SrcOperandAttr srcResource,
                           dxsa::SrcOperandAttr srcSampler,
                           dxsa::SrcOperandAttr srcLod,
                           dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::SampleL::create(builder, loc, dst, srcAddress, srcResource,
                                 srcSampler, srcLod, offset);
  }

  Instruction buildSampleLFeedback(
      dxsa::DstOperandAttr dst, dxsa::SrcOperandAttr srcAddress,
      dxsa::SrcOperandAttr srcResource, dxsa::SrcOperandAttr srcSampler,
      dxsa::SrcOperandAttr srcLod, dxsa::DstOperandAttr feedback,
      dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::SampleLFeedback::create(builder, loc, dst, srcAddress,
                                         srcResource, srcSampler, srcLod,
                                         feedback, offset);
  }

  Instruction buildSampleC(dxsa::DstOperandAttr dst,
                           dxsa::SrcOperandAttr srcAddress,
                           dxsa::SrcOperandAttr srcResource,
                           dxsa::SrcOperandAttr srcSampler,
                           dxsa::SrcOperandAttr srcReferenceValue,
                           dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::SampleC::create(builder, loc, dst, srcAddress, srcResource,
                                 srcSampler, srcReferenceValue, offset);
  }

  Instruction buildSampleCClampFeedback(
      dxsa::DstOperandAttr dst, dxsa::SrcOperandAttr srcAddress,
      dxsa::SrcOperandAttr srcResource, dxsa::SrcOperandAttr srcSampler,
      dxsa::SrcOperandAttr srcReferenceValue,
      dxsa::SampleClampFeedbackAttr clampFeedback,
      dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::SampleCClampFeedback::create(
        builder, loc, dst, srcAddress, srcResource, srcSampler,
        srcReferenceValue, clampFeedback, offset);
  }

  Instruction buildSampleCLZ(dxsa::DstOperandAttr dst,
                             dxsa::SrcOperandAttr srcAddress,
                             dxsa::SrcOperandAttr srcResource,
                             dxsa::SrcOperandAttr srcSampler,
                             dxsa::SrcOperandAttr srcReferenceValue,
                             dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::SampleCLZ::create(builder, loc, dst, srcAddress, srcResource,
                                   srcSampler, srcReferenceValue, offset);
  }

  Instruction buildSampleCLZFeedback(
      dxsa::DstOperandAttr dst, dxsa::SrcOperandAttr srcAddress,
      dxsa::SrcOperandAttr srcResource, dxsa::SrcOperandAttr srcSampler,
      dxsa::SrcOperandAttr srcReferenceValue, dxsa::DstOperandAttr feedback,
      dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::SampleCLZFeedback::create(
        builder, loc, dst, srcAddress, srcResource, srcSampler,
        srcReferenceValue, feedback, offset);
  }

  Instruction buildGather4(dxsa::DstOperandAttr dst,
                           dxsa::SrcOperandAttr srcAddress,
                           dxsa::SrcOperandAttr srcResource,
                           dxsa::SrcOperandAttr srcSampler,
                           dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::Gather4::create(builder, loc, dst, srcAddress, srcResource,
                                 srcSampler, offset);
  }

  Instruction buildGather4Feedback(dxsa::DstOperandAttr dst,
                                   dxsa::SrcOperandAttr srcAddress,
                                   dxsa::SrcOperandAttr srcResource,
                                   dxsa::SrcOperandAttr srcSampler,
                                   dxsa::DstOperandAttr feedback,
                                   dxsa::SampleOffsetAttr offset,
                                   Location loc) {
    return dxsa::Gather4Feedback::create(builder, loc, dst, srcAddress,
                                         srcResource, srcSampler, feedback,
                                         offset);
  }

  Instruction buildGather4C(dxsa::DstOperandAttr dst,
                            dxsa::SrcOperandAttr srcAddress,
                            dxsa::SrcOperandAttr srcResource,
                            dxsa::SrcOperandAttr srcSampler,
                            dxsa::SrcOperandAttr srcReferenceValue,
                            dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::Gather4C::create(builder, loc, dst, srcAddress, srcResource,
                                  srcSampler, srcReferenceValue, offset);
  }

  Instruction buildGather4CFeedback(
      dxsa::DstOperandAttr dst, dxsa::SrcOperandAttr srcAddress,
      dxsa::SrcOperandAttr srcResource, dxsa::SrcOperandAttr srcSampler,
      dxsa::SrcOperandAttr srcReferenceValue, dxsa::DstOperandAttr feedback,
      dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::Gather4CFeedback::create(builder, loc, dst, srcAddress,
                                          srcResource, srcSampler,
                                          srcReferenceValue, feedback, offset);
  }

  Instruction buildGather4PO(dxsa::DstOperandAttr dst,
                             dxsa::SrcOperandAttr srcAddress,
                             dxsa::SrcOperandAttr srcOffset,
                             dxsa::SrcOperandAttr srcResource,
                             dxsa::SrcOperandAttr srcSampler, Location loc) {
    return dxsa::Gather4PO::create(builder, loc, dst, srcAddress, srcOffset,
                                   srcResource, srcSampler);
  }

  Instruction buildGather4POFeedback(dxsa::DstOperandAttr dst,
                                     dxsa::SrcOperandAttr srcAddress,
                                     dxsa::SrcOperandAttr srcOffset,
                                     dxsa::SrcOperandAttr srcResource,
                                     dxsa::SrcOperandAttr srcSampler,
                                     dxsa::DstOperandAttr feedback,
                                     Location loc) {
    return dxsa::Gather4POFeedback::create(builder, loc, dst, srcAddress,
                                           srcOffset, srcResource, srcSampler,
                                           feedback);
  }

  Instruction buildGather4POC(dxsa::DstOperandAttr dst,
                              dxsa::SrcOperandAttr srcAddress,
                              dxsa::SrcOperandAttr srcOffset,
                              dxsa::SrcOperandAttr srcResource,
                              dxsa::SrcOperandAttr srcSampler,
                              dxsa::SrcOperandAttr srcReferenceValue,
                              Location loc) {
    return dxsa::Gather4POC::create(builder, loc, dst, srcAddress, srcOffset,
                                    srcResource, srcSampler, srcReferenceValue);
  }

  Instruction buildGather4POCFeedback(
      dxsa::DstOperandAttr dst, dxsa::SrcOperandAttr srcAddress,
      dxsa::SrcOperandAttr srcOffset, dxsa::SrcOperandAttr srcResource,
      dxsa::SrcOperandAttr srcSampler, dxsa::SrcOperandAttr srcReferenceValue,
      dxsa::DstOperandAttr feedback, Location loc) {
    return dxsa::Gather4POCFeedback::create(builder, loc, dst, srcAddress,
                                            srcOffset, srcResource, srcSampler,
                                            srcReferenceValue, feedback);
  }

  Instruction buildLd(dxsa::DstOperandAttr dst, dxsa::SrcOperandAttr srcAddress,
                      dxsa::SrcOperandAttr srcResource,
                      dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::Ld::create(builder, loc, dst, srcAddress, srcResource, offset);
  }

  Instruction buildLdFeedback(dxsa::DstOperandAttr dst,
                              dxsa::SrcOperandAttr srcAddress,
                              dxsa::SrcOperandAttr srcResource,
                              dxsa::DstOperandAttr feedback,
                              dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::LdFeedback::create(builder, loc, dst, srcAddress, srcResource,
                                    feedback, offset);
  }

  Instruction buildLd2dms(dxsa::DstOperandAttr dst,
                          dxsa::SrcOperandAttr srcAddress,
                          dxsa::SrcOperandAttr srcResource,
                          dxsa::SrcOperandAttr sampleIndex,
                          dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::Ld2dms::create(builder, loc, dst, srcAddress, srcResource,
                                sampleIndex, offset);
  }

  Instruction buildLd2dmsFeedback(dxsa::DstOperandAttr dst,
                                  dxsa::SrcOperandAttr srcAddress,
                                  dxsa::SrcOperandAttr srcResource,
                                  dxsa::SrcOperandAttr sampleIndex,
                                  dxsa::DstOperandAttr feedback,
                                  dxsa::SampleOffsetAttr offset, Location loc) {
    return dxsa::Ld2dmsFeedback::create(builder, loc, dst, srcAddress,
                                        srcResource, sampleIndex, feedback,
                                        offset);
  }

  Instruction buildLdRaw(dxsa::DstOperandAttr dst,
                         dxsa::SrcOperandAttr srcByteOffset,
                         dxsa::SrcOperandAttr src, Location loc) {
    return dxsa::LdRaw::create(builder, loc, dst, srcByteOffset, src);
  }

  Instruction buildLdRawFeedback(dxsa::DstOperandAttr dst,
                                 dxsa::SrcOperandAttr srcByteOffset,
                                 dxsa::SrcOperandAttr src,
                                 dxsa::DstOperandAttr feedback, Location loc) {
    return dxsa::LdRawFeedback::create(builder, loc, dst, srcByteOffset, src,
                                       feedback);
  }

  Instruction buildLdStructured(dxsa::DstOperandAttr dst,
                                dxsa::SrcOperandAttr srcAddress,
                                dxsa::SrcOperandAttr srcByteOffset,
                                dxsa::SrcOperandAttr src, Location loc) {
    return dxsa::LdStructured::create(builder, loc, dst, srcAddress,
                                      srcByteOffset, src);
  }

  Instruction buildLdStructuredFeedback(dxsa::DstOperandAttr dst,
                                        dxsa::SrcOperandAttr srcAddress,
                                        dxsa::SrcOperandAttr srcByteOffset,
                                        dxsa::SrcOperandAttr src,
                                        dxsa::DstOperandAttr feedback,
                                        Location loc) {
    return dxsa::LdStructuredFeedback::create(builder, loc, dst, srcAddress,
                                              srcByteOffset, src, feedback);
  }

  Instruction buildLdUavTyped(dxsa::DstOperandAttr dst,
                              dxsa::SrcOperandAttr srcAddress,
                              dxsa::SrcOperandAttr srcUav, Location loc) {
    return dxsa::LdUavTyped::create(builder, loc, dst, srcAddress, srcUav);
  }

  Instruction buildLdUavTypedFeedback(dxsa::DstOperandAttr dst,
                                      dxsa::SrcOperandAttr srcAddress,
                                      dxsa::SrcOperandAttr srcUav,
                                      dxsa::DstOperandAttr feedback,
                                      Location loc) {
    return dxsa::LdUavTypedFeedback::create(builder, loc, dst, srcAddress,
                                            srcUav, feedback);
  }

private:
  MLIRContext *context;
  OpBuilder builder;
};

class Parser {
public:
  Parser(DXBuilder &builder, StringAttr name, StringRef buffer)
      : builder(builder), name(name), buffer(buffer) {
    initInstructionInfo(instrInfo);
  }

  using Token = FailureOr<uint32_t>;
  using Index = DXBuilder::Index;
  using Operand = DXBuilder::Operand;
  using Instruction = DXBuilder::Instruction;
  using Module = DXBuilder::Module;

  /// Width of the token in the program binary stream.
  static constexpr size_t tokenSize = sizeof(uint32_t);
  uint32_t getRemainingBytes() { return buffer.size() - currentTokenOffset; }

  /// Parse the current token and move the cursor to the next one.
  Token parseToken() {
    if (getRemainingBytes() < tokenSize) {
      return emitError(getLocation(), "unexpected end of file");
    }

    auto value = support::endian::read<uint32_t>(
        buffer.begin() + currentTokenOffset, endianness::little);
    currentTokenOffset += tokenSize;

    return value;
  }

  FailureOr<SmallVector<uint32_t>> parseTokens(uint32_t numTokens) {
    SmallVector<uint32_t> tokens(numTokens);
    for (uint32_t i = 0; i < numTokens; ++i) {
      auto token = parseToken();
      if (failed(token))
        return failure();
      tokens[i] = *token;
    }
    return tokens;
  }

  /// Returns location where the last parsed token begins (at offset
  /// -4 from the currentTokenOffset).
  FileLineColLoc getLocation(int offset = -4) const {
    return FileLineColLoc::get(name, 0, currentTokenOffset + offset);
  }

  bool isImmOperand(uint32_t token) {
    switch (DECODE_D3D10_SB_OPERAND_TYPE(token)) {
    case D3D10_SB_OPERAND_TYPE_IMMEDIATE32:
    case D3D10_SB_OPERAND_TYPE_IMMEDIATE64:
      return true;
    default:
      return false;
    }
  }

  FailureOr<OperandComponents> parseOperandComponents(uint32_t token) {
    OperandComponents components{};
    switch (DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(token)) {
    case D3D10_SB_OPERAND_0_COMPONENT: {
      components.num = 0;
      break;
    }
    case D3D10_SB_OPERAND_1_COMPONENT: {
      components.num = 1;
      break;
    }
    case D3D10_SB_OPERAND_4_COMPONENT: {
      components.num = 4;
      break;
    }
    default:
      emitError(getLocation(), "unexpected number of components");
      return failure();
    }

    if (components.num != 4 || isImmOperand(token))
      return components;

    switch (DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(token)) {
    case D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE: {
      components.kind = OperandComponentsKind::Mask;
      components.mask = DECODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(token);
      break;
    }
    case D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE: {
      components.kind = OperandComponentsKind::Swizzle;
      components.swizzle[0] =
          DECODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_SOURCE(token, 0);
      components.swizzle[1] =
          DECODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_SOURCE(token, 1);
      components.swizzle[2] =
          DECODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_SOURCE(token, 2);
      components.swizzle[3] =
          DECODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_SOURCE(token, 3);
      break;
    }
    case D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE: {
      components.kind = OperandComponentsKind::One;
      components.one = DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(token);
      break;
    }
    default:
      emitError(getLocation(), "unexpected component selection");
      return failure();
    }

    return components;
  }

  using OperandIndexTypes = SmallVector<uint32_t, 3>;

  FailureOr<OperandIndexTypes> parseOperandIndexTypes(uint32_t token) {
    SmallVector<uint32_t, 3> indexTypes;
    if (isImmOperand(token))
      return indexTypes; // none

    uint32_t indexDimension = DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(token);
    if (indexDimension > 3) {
      emitError(getLocation(),
                "invalid operand index dimension (must be <= 3)");
      return failure();
    }

    if (indexDimension == D3D10_SB_OPERAND_INDEX_0D)
      return indexTypes; // none

    indexTypes.resize(indexDimension);
    for (unsigned i = 0; i < indexTypes.size(); ++i) {
      indexTypes[i] = DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(i, token);
    }

    return indexTypes;
  }

  FailureOr<Index> parseIndex(uint32_t indexType) {
    switch (indexType) {
    case D3D10_SB_OPERAND_INDEX_IMMEDIATE32: {
      Token value = parseToken();
      if (failed(value)) {
        emitError(getLocation(), "expected an operand index imm32");
        return failure();
      }
      return builder.buildIndexImm32(*value, getLocation());
    }
    case D3D10_SB_OPERAND_INDEX_IMMEDIATE64: {
      Token value0 = parseToken();
      if (failed(value0)) {
        emitError(getLocation(), "expected an operand index imm64");
        return failure();
      }

      FileLineColLoc loc = getLocation();

      Token value1 = parseToken();
      if (failed(value1)) {
        emitError(getLocation(),
                  "expected an operand index imm64 (second token)");
        return failure();
      }

      // TODO: check the order of tokens (MSB or LSB?)
      return builder.buildIndexImm64((((uint64_t)*value0) << 32) | *value1,
                                     loc);
    }
    case D3D10_SB_OPERAND_INDEX_RELATIVE: {
      FailureOr<Operand> operand = parseOperand();
      if (failed(operand)) {
        emitError(getLocation(), "expected an index operand");
        return failure();
      }
      return builder.buildIndexRelative(*operand, getLocation());
    }
    case D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE: {
      Token imm = parseToken();
      if (failed(imm)) {
        emitError(getLocation(), "expected an operand index relative (imm)");
        return failure();
      }

      FileLineColLoc loc = getLocation();

      FailureOr<Operand> operand = parseOperand();
      if (failed(operand)) {
        emitError(getLocation(),
                  "expected an operand index relative (operand)");
        return failure();
      }

      return builder.buildIndexImm32PlusRelative(*imm, *operand, loc);
    }
    default:
      emitError(getLocation(), "invalid operand index type");
      return failure();
    }
  }

  FailureOr<std::optional<OperandModifier>>
  parseOperandExtendedModifier(uint32_t extToken) {
    std::optional<OperandModifier> none;
    if (D3D10_SB_EXTENDED_OPERAND_MODIFIER !=
        DECODE_D3D10_SB_EXTENDED_OPERAND_TYPE(extToken))
      return none;

    OperandModifier modifier;
    modifier.modifier = DECODE_D3D10_SB_OPERAND_MODIFIER(extToken);
    modifier.minPrecision = DECODE_D3D11_SB_OPERAND_MIN_PRECISION(extToken);
    modifier.nonUniform = DECODE_D3D12_SB_OPERAND_NON_UNIFORM(extToken);
    return std::make_optional(modifier);
  }

  FailureOr<Operand> parseOperand() {
    Token token = parseToken();
    if (failed(token))
      return failure();

    FileLineColLoc loc = getLocation();

    uint32_t opType = DECODE_D3D10_SB_OPERAND_TYPE(*token);
    bool isExtended = DECODE_IS_D3D10_SB_OPERAND_EXTENDED(*token);

    FailureOr<OperandComponents> components = parseOperandComponents(*token);
    if (failed(components))
      return failure();

    FailureOr<OperandIndexTypes> indexTypes = parseOperandIndexTypes(*token);
    if (failed(indexTypes))
      return failure();

    std::optional<OperandModifier> modifier;
    if (isExtended) {
      Token extToken = parseToken();
      if (failed(extToken)) {
        emitError(getLocation(), "unexpected an extended operand token");
        return failure();
      }
      auto failureOrModifier = parseOperandExtendedModifier(*extToken);
      if (failed(failureOrModifier))
        return failure();
      modifier = *failureOrModifier;
    }

    if (isImmOperand(*token)) {
      switch (opType) {
      case D3D10_SB_OPERAND_TYPE_IMMEDIATE32: {
        SmallVector<int32_t, 4> values;
        for (unsigned i = 0; i < components->num; ++i) {
          Token value = parseToken();
          if (failed(value)) {
            emitError(getLocation(), "expected an immediate operand (imm32)");
            return failure();
          }
          values.push_back(*value);
        }
        return builder.buildOperandImm32(values, loc);
      }
      case D3D10_SB_OPERAND_TYPE_IMMEDIATE64: {
        if (components->num != 4) {
          emitError(getLocation(), "imm64 operand must have 4 components");
          return failure();
        }
        SmallVector<int64_t, 2> values;
        for (unsigned i = 0; i < 2; ++i) {
          // A 64-bit immediate is stored as two DWORDs, low half first.
          Token low = parseToken();
          if (failed(low)) {
            emitError(getLocation(),
                      "expected an immediate operand (imm64 low)");
            return failure();
          }

          Token high = parseToken();
          if (failed(high)) {
            emitError(getLocation(),
                      "expected an immediate operand (imm64 high)");
            return failure();
          }

          values.push_back((((int64_t)*high) << 32) | *low);
        }
        return builder.buildOperandImm64(values, loc);
      }
      }
      emitError(getLocation(), "unhandled immediate type");
      return failure();
    }

    // Operand indices
    SmallVector<Index, 3> indices;
    for (uint32_t indexType : *indexTypes) {
      FailureOr<Index> index = parseIndex(indexType);
      if (failed(index))
        return failure();
      indices.push_back(*index);
    }
    return builder.buildOperand(opType, *components, indices, modifier,
                                getLocation());
  }

  FailureOr<Instruction> parseDclGlobalFlags(uint32_t opcodeToken,
                                             Location loc) {
    auto raw = DECODE_D3D10_SB_GLOBAL_FLAGS(opcodeToken);
    if (raw == 0) {
      emitError(loc, "expected at least one global flag to be set");
      return failure();
    }
    auto flags = static_cast<dxsa::GlobalFlags>(0);
    if (raw & D3D10_SB_GLOBAL_FLAG_REFACTORING_ALLOWED)
      flags |= dxsa::GlobalFlags::refactoringAllowed;
    if (raw & D3D11_SB_GLOBAL_FLAG_ENABLE_DOUBLE_PRECISION_FLOAT_OPS)
      flags |= dxsa::GlobalFlags::enableDoublePrecisionFloatOps;
    if (raw & D3D11_SB_GLOBAL_FLAG_FORCE_EARLY_DEPTH_STENCIL)
      flags |= dxsa::GlobalFlags::forceEarlyDepthStencil;
    if (raw & D3D11_SB_GLOBAL_FLAG_ENABLE_RAW_AND_STRUCTURED_BUFFERS)
      flags |= dxsa::GlobalFlags::enableRawAndStructuredBuffers;
    if (raw & D3D11_1_SB_GLOBAL_FLAG_SKIP_OPTIMIZATION)
      flags |= dxsa::GlobalFlags::skipOptimization;
    if (raw & D3D11_1_SB_GLOBAL_FLAG_ENABLE_MINIMUM_PRECISION)
      flags |= dxsa::GlobalFlags::enableMinimumPrecision;
    if (raw & D3D11_1_SB_GLOBAL_FLAG_ENABLE_DOUBLE_EXTENSIONS)
      flags |= dxsa::GlobalFlags::enableDoubleExtensions;
    if (raw & D3D11_1_SB_GLOBAL_FLAG_ENABLE_SHADER_EXTENSIONS)
      flags |= dxsa::GlobalFlags::enableShaderExtensions;
    if (raw & D3D12_SB_GLOBAL_FLAG_ALL_RESOURCES_BOUND)
      flags |= dxsa::GlobalFlags::allResourcesBound;
    return builder.buildDclGlobalFlags(flags, loc);
  }

  FailureOr<Instruction> parseSync(uint32_t opcodeToken, size_t beginOffset,
                                   uint32_t length, Location loc) {
    auto raw = DECODE_D3D11_SB_SYNC_FLAGS(opcodeToken);
    if (raw == 0)
      return emitError(loc, "expected at least one sync flag to be set");
    auto flags = static_cast<dxsa::SyncFlags>(0);
    if (raw & D3D11_SB_SYNC_UNORDERED_ACCESS_VIEW_MEMORY_GLOBAL)
      flags |= dxsa::SyncFlags::uav_global;
    if (raw & D3D11_SB_SYNC_UNORDERED_ACCESS_VIEW_MEMORY_GROUP)
      flags |= dxsa::SyncFlags::uav_group;
    if (raw & D3D11_SB_SYNC_THREAD_GROUP_SHARED_MEMORY)
      flags |= dxsa::SyncFlags::tgsm;
    if (raw & D3D11_SB_SYNC_THREADS_IN_GROUP)
      flags |= dxsa::SyncFlags::threads;
    if (failed(verifyInstructionLength(beginOffset, length)))
      return failure();
    return builder.buildSync(flags, loc);
  }

  FailureOr<Instruction> parseDclTemps(Location loc) {
    auto countToken = parseToken();
    if (failed(countToken))
      return failure();
    auto count = *countToken;
    if (count == 0) {
      emitError(getLocation(), "temp register count cannot be zero");
      return failure();
    }
    if (count > 4096) {
      emitError(getLocation(), "invalid temp register count: ")
          << count << " (max 4096)";
      return failure();
    }
    return builder.buildDclTemps(count, loc);
  }

  FailureOr<Instruction> parseDclInputControlPointCount(uint32_t opcodeToken,
                                                        Location loc) {
    auto count = DECODE_D3D11_SB_INPUT_CONTROL_POINT_COUNT(opcodeToken);
    if (count == 0) {
      emitError(loc, "input control point count cannot be zero");
      return failure();
    }
    if (count > 32) {
      emitError(loc, "input control point count must be <= 32, got ") << count;
      return failure();
    }
    return builder.buildDclInputControlPointCount(count, loc);
  }

  FailureOr<Instruction> parseDclOutputControlPointCount(uint32_t opcodeToken,
                                                         Location loc) {
    auto count = DECODE_D3D11_SB_OUTPUT_CONTROL_POINT_COUNT(opcodeToken);
    if (count > 32) {
      emitError(loc, "output control point count must be <= 32, got ") << count;
      return failure();
    }
    return builder.buildDclOutputControlPointCount(count, loc);
  }

  FailureOr<Instruction> parseDclTessellatorDomain(uint32_t opcodeToken,
                                                   Location loc) {
    auto rawDomain = DECODE_D3D11_SB_TESS_DOMAIN(opcodeToken);
    auto domain = dxsa::symbolizeTessellatorDomain(rawDomain);
    if (!domain)
      return emitError(loc, "unknown tessellator domain: ") << rawDomain;
    return builder.buildDclTessellatorDomain(*domain, loc);
  }

  FailureOr<Instruction>
  parseDclTessellatorOutputPrimitive(uint32_t opcodeToken, Location loc) {
    auto rawOutputPrimitiveType =
        DECODE_D3D11_SB_TESS_OUTPUT_PRIMITIVE(opcodeToken);
    auto outputPrimitiveType =
        dxsa::symbolizeTessellatorOutputPrimitiveType(rawOutputPrimitiveType);
    if (!outputPrimitiveType)
      return emitError(loc, "unknown tessellator output primitive type: ")
             << rawOutputPrimitiveType;
    return builder.buildDclTessellatorOutputPrimitive(*outputPrimitiveType,
                                                      loc);
  }

  FailureOr<Instruction> parseDclOutputTopology(uint32_t opcodeToken,
                                                Location loc) {
    auto rawOutputTopology =
        DECODE_D3D10_SB_GS_OUTPUT_PRIMITIVE_TOPOLOGY(opcodeToken);
    auto outputTopology =
        dxsa::symbolizeOutputPrimitiveTopology(rawOutputTopology);
    if (!outputTopology)
      return emitError(loc, "unknown output primitive topology: ")
             << rawOutputTopology;
    return builder.buildDclOutputTopology(*outputTopology, loc);
  }

  FailureOr<Instruction> parseDclTessellatorPartitioning(uint32_t opcodeToken,
                                                         Location loc) {
    auto rawPartitioningMode = DECODE_D3D11_SB_TESS_PARTITIONING(opcodeToken);
    auto partitioningMode =
        dxsa::symbolizeTessellatorPartitioningMode(rawPartitioningMode);
    if (!partitioningMode)
      return emitError(loc, "unknown tessellator partitioning mode: ")
             << rawPartitioningMode;
    return builder.buildDclTessellatorPartitioning(*partitioningMode, loc);
  }

  FailureOr<Instruction> parseDclInputPrimitive(uint32_t opcodeToken,
                                                Location loc) {
    auto rawInputPrimitive = DECODE_D3D10_SB_GS_INPUT_PRIMITIVE(opcodeToken);
    auto inputPrimitive = dxsa::symbolizeInputPrimitive(rawInputPrimitive);
    if (!inputPrimitive)
      return emitError(loc, "unknown input primitive: ") << rawInputPrimitive;
    return builder.buildDclInputPrimitive(*inputPrimitive, loc);
  }

  FailureOr<Instruction> parseDclGsInstanceCount(Location loc) {
    auto countToken = parseToken();
    FAILURE_IF_FAILED(countToken);
    auto count = *countToken;
    if (count == 0)
      return emitError(loc, "instance count cannot be zero");
    if (count > 32)
      return emitError(loc, "instance count must be <= 32, got ") << count;
    return builder.buildDclGsInstanceCount(count, loc);
  }

  FailureOr<Instruction> parseDclMaxOutputVertexCount(Location loc) {
    auto countToken = parseToken();
    FAILURE_IF_FAILED(countToken);
    auto count = *countToken;
    if (count == 0)
      return emitError(getLocation(), "max output vertex count cannot be zero");
    if (count > 1024)
      return emitError(getLocation(),
                       "max output vertex count must be <= 1024, got ")
             << count;
    return builder.buildDclMaxOutputVertexCount(count, loc);
  }

  template <typename SrcOrDstOperand>
  FailureOr<uint32_t> parseGsStreamIndex(SrcOrDstOperand operand,
                                         Location loc) {
    if (operand.getType() != dxsa::OperandType::m)
      return emitError(loc, "unexpected operand type: ")
             << dxsa::stringifyOperandType(operand.getType());
    if (operand.getComponents().getValue() != dxsa::OperandComponents::none)
      return emitError(loc, "unexpected operand components: ")
             << dxsa::stringifyOperandComponents(
                    operand.getComponents().getValue());
    auto indices = getRequiredImmIndices(operand, loc);
    FAILURE_IF_FAILED(indices);
    if (indices->size() != 1)
      return emitError(loc, "unsupported index dimension: ") << indices->size();
    return (*indices)[0];
  }

  FailureOr<Instruction> parseDclStream(Location loc) {
    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    auto index = parseGsStreamIndex(*operand, loc);
    FAILURE_IF_FAILED(index);
    return builder.buildGsStreamIndexOp<dxsa::DclStream>(*index, loc);
  }

  FailureOr<dxsa::InterpolationMode>
  parseInterpolationMode(uint32_t opcodeToken, Location loc) {
    auto rawInterpolationMode =
        DECODE_D3D10_SB_INPUT_INTERPOLATION_MODE(opcodeToken);
    auto interpolationMode =
        dxsa::symbolizeInterpolationMode(rawInterpolationMode);
    if (!interpolationMode)
      return emitError(loc, "unknown interpolation mode: ")
             << rawInterpolationMode;
    return *interpolationMode;
  }

  FailureOr<dxsa::SystemValueName> parseSystemValueName(Location loc) {
    Token nameToken = parseToken();
    if (failed(nameToken))
      return failure();
    auto rawSystemValueName = DECODE_D3D10_SB_NAME(*nameToken);
    auto systemValueName = dxsa::symbolizeSystemValueName(rawSystemValueName);
    if (!systemValueName)
      return emitError(loc, "unknown system value name: ")
             << rawSystemValueName;
    return *systemValueName;
  }

  FailureOr<Instruction> parseDclInputPs(uint32_t opcodeToken, Location loc) {
    auto interpolationMode = parseInterpolationMode(opcodeToken, loc);
    FAILURE_IF_FAILED(interpolationMode);
    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    return builder.buildDclInputPs(*interpolationMode, *operand, loc);
  }

  FailureOr<Instruction> parseDclInputPsSiv(uint32_t opcodeToken,
                                            Location loc) {
    auto interpolationMode = parseInterpolationMode(opcodeToken, loc);
    FAILURE_IF_FAILED(interpolationMode);
    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    auto systemValueName = parseSystemValueName(getLocation());
    FAILURE_IF_FAILED(systemValueName);
    return builder.buildDclInputPsSiv(*interpolationMode, *operand,
                                      *systemValueName, loc);
  }

  FailureOr<Instruction> parseDclInputPsSgv(Location loc) {
    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    auto systemValueName = parseSystemValueName(getLocation());
    FAILURE_IF_FAILED(systemValueName);
    return builder.buildDclInputPsSgv(*operand, *systemValueName, loc);
  }

  struct OperandFields {
    dxsa::OperandType type;
    OperandComponents components;
    SmallVector<dxsa::IndexAttr, 3> indexEntries;
    std::optional<OperandModifier> modifier;
    SmallVector<int32_t, 4> values;
    SmallVector<int64_t, 2> values64;
  };

  FailureOr<dxsa::IndexAttr> parseOperandIndex(uint32_t indexType) {
    switch (indexType) {
    case D3D10_SB_OPERAND_INDEX_IMMEDIATE32: {
      auto value = parseToken();
      FAILURE_IF_FAILED(value);
      return builder.buildOperandIndexImm32(static_cast<int32_t>(*value));
    }
    case D3D10_SB_OPERAND_INDEX_IMMEDIATE64: {
      auto high = parseToken();
      FAILURE_IF_FAILED(high);
      auto low = parseToken();
      FAILURE_IF_FAILED(low);
      return builder.buildOperandIndexImm64((((int64_t)*high) << 32) | *low);
    }
    case D3D10_SB_OPERAND_INDEX_RELATIVE: {
      auto relative = parseSrcOperand();
      FAILURE_IF_FAILED(relative);
      return builder.buildOperandIndexRelative(*relative);
    }
    case D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE: {
      auto value = parseToken();
      FAILURE_IF_FAILED(value);
      auto relative = parseSrcOperand();
      FAILURE_IF_FAILED(relative);
      return builder.buildOperandIndexImm32PlusRelative(
          static_cast<int32_t>(*value), *relative);
    }
    case D3D10_SB_OPERAND_INDEX_IMMEDIATE64_PLUS_RELATIVE: {
      auto high = parseToken();
      FAILURE_IF_FAILED(high);
      auto low = parseToken();
      FAILURE_IF_FAILED(low);
      auto relative = parseSrcOperand();
      FAILURE_IF_FAILED(relative);
      return builder.buildOperandIndexImm64PlusRelative(
          (((int64_t)*high) << 32) | *low, *relative);
    }
    default:
      return emitError(getLocation(), "invalid operand index representation: ")
             << indexType;
    }
  }

  FailureOr<OperandFields> parseOperandFields() {
    auto token = parseToken();
    FAILURE_IF_FAILED(token);

    auto loc = getLocation();
    auto rawOperandType = DECODE_D3D10_SB_OPERAND_TYPE(*token);
    auto isExtended = DECODE_IS_D3D10_SB_OPERAND_EXTENDED(*token);

    auto type = dxsa::symbolizeOperandType(rawOperandType);
    if (!type)
      // Streaming the raw enum would print the value as a byte, use cast to
      // prevent it.
      return emitError(loc, "unknown operand type: ")
             << static_cast<unsigned>(rawOperandType);

    auto components = parseOperandComponents(*token);
    FAILURE_IF_FAILED(components);

    auto indexTypes = parseOperandIndexTypes(*token);
    FAILURE_IF_FAILED(indexTypes);

    OperandFields decoded;
    decoded.type = *type;
    decoded.components = *components;

    if (isExtended) {
      auto extToken = parseToken();
      FAILURE_IF_FAILED(extToken);
      auto opMod = parseOperandExtendedModifier(*extToken);
      FAILURE_IF_FAILED(opMod);
      decoded.modifier = *opMod;
    }

    if (isImmOperand(*token)) {
      if (rawOperandType == D3D10_SB_OPERAND_TYPE_IMMEDIATE64) {
        for (unsigned i = 0; i < 2; ++i) {
          // A 64-bit immediate is stored as two DWORDs, low half first.
          auto low = parseToken();
          FAILURE_IF_FAILED(low);
          auto high = parseToken();
          FAILURE_IF_FAILED(high);
          decoded.values64.push_back((((int64_t)*high) << 32) | *low);
        }
        return decoded;
      }
      for (uint32_t i = 0; i < components->num; ++i) {
        auto value = parseToken();
        FAILURE_IF_FAILED(value);
        decoded.values.push_back(static_cast<int32_t>(*value));
      }
      return decoded;
    }

    for (uint32_t indexType : *indexTypes) {
      auto entry = parseOperandIndex(indexType);
      FAILURE_IF_FAILED(entry);
      decoded.indexEntries.push_back(*entry);
    }
    return decoded;
  }

  FailureOr<dxsa::DstOperandAttr> parseDstOperand() {
    auto loc = getLocation();
    auto fields = parseOperandFields();
    FAILURE_IF_FAILED(fields);
    if (!fields->values.empty() || !fields->values64.empty())
      return emitError(loc, "immediate operand `")
             << dxsa::stringifyOperandType(fields->type)
             << "` cannot be a destination";
    return builder.buildDstOperandAttr(fields->type, fields->components,
                                       fields->indexEntries, fields->modifier);
  }

  FailureOr<dxsa::SrcOperandAttr> parseSrcOperand() {
    auto fields = parseOperandFields();
    FAILURE_IF_FAILED(fields);
    return builder.buildSrcOperandAttr(fields->type, fields->components,
                                       fields->indexEntries, fields->modifier,
                                       fields->values, fields->values64);
  }

  // Get plain immediates with no relative indices.
  template <typename SrcOrDstOperand>
  FailureOr<SmallVector<uint32_t, 3>>
  getRequiredImmIndices(SrcOrDstOperand operand, Location loc) {
    SmallVector<uint32_t, 3> indices;
    if (auto index = operand.getIndex())
      for (dxsa::IndexAttr entry : index) {
        if (entry.getRelative() || !entry.getImm())
          return emitError(loc, "operand index must be immediate");
        indices.push_back(static_cast<uint32_t>(entry.getImm().getInt()));
      }
    return indices;
  }

  template <std::size_t N, typename OperandT>
  FailureOr<std::array<OperandT, N>>
  parseNOperands(llvm::function_ref<FailureOr<OperandT>()> parseOperand) {
    std::array<OperandT, N> operands;
    for (auto &operand : operands) {
      auto parsed = parseOperand();
      FAILURE_IF_FAILED(parsed);
      operand = *parsed;
    }
    return operands;
  }

  template <typename OpT, typename OpSatT, HasPreciseAttr HasPrecise,
            std::size_t NumDstOperands, std::size_t NumSrcOperands>
  FailureOr<Instruction> decodeOp(size_t beginOffset, uint32_t length,
                                  const InstructionModifier &modifier,
                                  Location loc) {
    auto dsts = parseNOperands<NumDstOperands, dxsa::DstOperandAttr>(
        [this] { return parseDstOperand(); });
    FAILURE_IF_FAILED(dsts);
    auto srcs = parseNOperands<NumSrcOperands, dxsa::SrcOperandAttr>(
        [this] { return parseSrcOperand(); });
    FAILURE_IF_FAILED(srcs);
    if (failed(verifyInstructionLength(beginOffset, length)))
      return failure();
    if constexpr (!std::is_same_v<OpSatT, OpT>)
      if (modifier.saturate)
        return builder.buildOp<OpSatT, HasPrecise>(modifier.preciseMask, loc,
                                                   *dsts, *srcs);
    return builder.buildOp<OpT, HasPrecise>(modifier.preciseMask, loc, *dsts,
                                            *srcs);
  }

  template <typename OpT>
  FailureOr<Instruction> decodeStreamIndexOp(size_t beginOffset,
                                             uint32_t length, Location loc) {
    auto operand = parseSrcOperand();
    FAILURE_IF_FAILED(operand);
    auto index = parseGsStreamIndex(*operand, loc);
    FAILURE_IF_FAILED(index);
    if (failed(verifyInstructionLength(beginOffset, length)))
      return failure();
    return builder.buildGsStreamIndexOp<OpT>(*index, loc);
  }

  void parseExtendedInstruction(uint32_t extendedToken,
                                ExtendedInstruction &ext) {
    switch (DECODE_D3D10_SB_EXTENDED_OPCODE_TYPE(extendedToken)) {
    case D3D10_SB_EXTENDED_OPCODE_EMPTY:
      return;
    case D3D10_SB_EXTENDED_OPCODE_SAMPLE_CONTROLS: {
      auto token = static_cast<int32_t>(extendedToken);
      int32_t offsets[3] = {
          DECODE_IMMEDIATE_D3D10_SB_ADDRESS_OFFSET(
              D3D10_SB_IMMEDIATE_ADDRESS_OFFSET_U, token),
          DECODE_IMMEDIATE_D3D10_SB_ADDRESS_OFFSET(
              D3D10_SB_IMMEDIATE_ADDRESS_OFFSET_V, token),
          DECODE_IMMEDIATE_D3D10_SB_ADDRESS_OFFSET(
              D3D10_SB_IMMEDIATE_ADDRESS_OFFSET_W, token),
      };
      for (int32_t &offset : offsets) {
        // Sign extend from 4 bits to 32.
        if (offset & 0x8) {
          offset |= 0xfffffff0;
        }
      }
      ext.sampleOffset = {offsets[0], offsets[1], offsets[2]};
      return;
    }
    case D3D11_SB_EXTENDED_OPCODE_RESOURCE_DIM: {
      auto dim = DECODE_D3D11_SB_EXTENDED_RESOURCE_DIMENSION(extendedToken);
      auto stride =
          (dim == D3D11_SB_RESOURCE_DIMENSION_STRUCTURED_BUFFER)
              ? std::optional<uint32_t>(
                    DECODE_D3D11_SB_EXTENDED_RESOURCE_DIMENSION_STRUCTURE_STRIDE(
                        extendedToken))
              : std::nullopt;
      ext.resourceDim = {dim, stride};
      return;
    }
    case D3D11_SB_EXTENDED_OPCODE_RESOURCE_RETURN_TYPE: {
      ext.resourceReturnType = {
          DECODE_D3D11_SB_EXTENDED_RESOURCE_RETURN_TYPE(extendedToken, 0),
          DECODE_D3D11_SB_EXTENDED_RESOURCE_RETURN_TYPE(extendedToken, 1),
          DECODE_D3D11_SB_EXTENDED_RESOURCE_RETURN_TYPE(extendedToken, 2),
          DECODE_D3D11_SB_EXTENDED_RESOURCE_RETURN_TYPE(extendedToken, 3),
      };
      return;
    }
    }
  }

  FailureOr<Instruction> parseSampleInstruction(uint32_t opcode,
                                                ExtendedInstruction &ext,
                                                size_t beginOffset,
                                                uint32_t length, Location loc) {
    dxsa::SampleOffsetAttr offset;
    if (ext.sampleOffset) {
      offset = builder.buildSampleOffsetAttr(*ext.sampleOffset);
    }

    auto dst = parseDstOperand();
    FAILURE_IF_FAILED(dst);

    dxsa::DstOperandAttr feedback;
    switch (opcode) {
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_CLAMP_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_D_CLAMP_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_B_CLAMP_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_L_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_C_CLAMP_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_C_LZ_FEEDBACK:
      auto op = parseDstOperand();
      FAILURE_IF_FAILED(op);
      feedback = *op;
      break;
    }

    auto srcAddress = parseSrcOperand();
    FAILURE_IF_FAILED(srcAddress);

    auto srcResource = parseSrcOperand();
    FAILURE_IF_FAILED(srcResource);

    auto srcSampler = parseSrcOperand();
    FAILURE_IF_FAILED(srcSampler);

    FailureOr<Instruction> instr;
    switch (opcode) {
    case D3D10_SB_OPCODE_SAMPLE:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_CLAMP_FEEDBACK: {
      if (feedback) {
        auto clamp = parseSrcOperand();
        FAILURE_IF_FAILED(clamp);

        auto clampFeedback =
            builder.buildSampleClampFeedbackAttr(*clamp, feedback);
        instr = builder.buildSampleClampFeedback(*dst, *srcAddress,
                                                 *srcResource, *srcSampler,
                                                 clampFeedback, offset, loc);
      } else {
        instr = builder.buildSample(*dst, *srcAddress, *srcResource,
                                    *srcSampler, offset, loc);
      }
      break;
    }
    case D3D10_SB_OPCODE_SAMPLE_D:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_D_CLAMP_FEEDBACK: {
      auto srcXDerivatives = parseSrcOperand();
      FAILURE_IF_FAILED(srcXDerivatives);

      auto srcYDerivatives = parseSrcOperand();
      FAILURE_IF_FAILED(srcYDerivatives);

      if (feedback) {
        auto clamp = parseSrcOperand();
        FAILURE_IF_FAILED(clamp);

        auto clampFeedback =
            builder.buildSampleClampFeedbackAttr(*clamp, feedback);
        instr = builder.buildSampleDClampFeedback(
            *dst, *srcAddress, *srcResource, *srcSampler, *srcXDerivatives,
            *srcYDerivatives, clampFeedback, offset, loc);
      } else {
        instr = builder.buildSampleD(*dst, *srcAddress, *srcResource,
                                     *srcSampler, *srcXDerivatives,
                                     *srcYDerivatives, offset, loc);
      }
      break;
    }
    case D3D10_SB_OPCODE_SAMPLE_B:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_B_CLAMP_FEEDBACK: {
      auto srcLodBias = parseSrcOperand();
      FAILURE_IF_FAILED(srcLodBias);

      if (feedback) {
        auto clamp = parseSrcOperand();
        FAILURE_IF_FAILED(clamp);
        auto clampFeedback =
            builder.buildSampleClampFeedbackAttr(*clamp, feedback);
        instr = builder.buildSampleBClampFeedback(
            *dst, *srcAddress, *srcResource, *srcSampler, *srcLodBias,
            clampFeedback, offset, loc);
      } else {
        instr = builder.buildSampleB(*dst, *srcAddress, *srcResource,
                                     *srcSampler, *srcLodBias, offset, loc);
      }
      break;
    }
    case D3D10_SB_OPCODE_SAMPLE_L:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_L_FEEDBACK: {
      auto srcLod = parseSrcOperand();
      FAILURE_IF_FAILED(srcLod);

      if (feedback) {
        instr = builder.buildSampleLFeedback(*dst, *srcAddress, *srcResource,
                                             *srcSampler, *srcLod, feedback,
                                             offset, loc);
      } else {
        instr = builder.buildSampleL(*dst, *srcAddress, *srcResource,
                                     *srcSampler, *srcLod, offset, loc);
      }
      break;
    }
    case D3D10_SB_OPCODE_SAMPLE_C:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_C_CLAMP_FEEDBACK: {
      auto srcReferenceValue = parseSrcOperand();
      FAILURE_IF_FAILED(srcReferenceValue);

      if (feedback) {
        auto clamp = parseSrcOperand();
        FAILURE_IF_FAILED(clamp);
        auto clampFeedback =
            builder.buildSampleClampFeedbackAttr(*clamp, feedback);
        instr = builder.buildSampleCClampFeedback(
            *dst, *srcAddress, *srcResource, *srcSampler, *srcReferenceValue,
            clampFeedback, offset, loc);
      } else {
        instr =
            builder.buildSampleC(*dst, *srcAddress, *srcResource, *srcSampler,
                                 *srcReferenceValue, offset, loc);
      }
      break;
    }
    case D3D10_SB_OPCODE_SAMPLE_C_LZ:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_C_LZ_FEEDBACK: {
      auto srcReferenceValue = parseSrcOperand();
      FAILURE_IF_FAILED(srcReferenceValue);

      if (feedback) {
        // `sample_c_lz_s` samples at LOD zero, so unlike the `_cl_s`
        // opcodes it carries no LOD clamp operand.
        instr = builder.buildSampleCLZFeedback(*dst, *srcAddress, *srcResource,
                                               *srcSampler, *srcReferenceValue,
                                               feedback, offset, loc);
      } else {
        instr =
            builder.buildSampleCLZ(*dst, *srcAddress, *srcResource, *srcSampler,
                                   *srcReferenceValue, offset, loc);
      }
      break;
    }
    default:
      llvm_unreachable("unhandled instruction");
    }

    FAILURE_IF_FAILED(instr);
    FAILURE_IF_FAILED(verifyInstructionLength(beginOffset, length));
    return instr;
  }

  FailureOr<Instruction>
  parseGather4Instructions(uint32_t opcode, ExtendedInstruction &ext,
                           size_t beginOffset, uint32_t length, Location loc) {
    dxsa::SampleOffsetAttr offset;
    if (ext.sampleOffset) {
      offset = builder.buildSampleOffsetAttr(*ext.sampleOffset);
    }

    auto dst = parseDstOperand();
    FAILURE_IF_FAILED(dst);

    dxsa::DstOperandAttr feedback;
    switch (opcode) {
    case D3DWDDM1_3_SB_OPCODE_GATHER4_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_GATHER4_C_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_GATHER4_PO_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_GATHER4_PO_C_FEEDBACK:
      // For Feedback variant, feedback operand is the second dst
      // register.
      auto op = parseDstOperand();
      FAILURE_IF_FAILED(op);
      feedback = *op;
      break;
    }

    auto srcAddress = parseSrcOperand();
    FAILURE_IF_FAILED(srcAddress);

    dxsa::SrcOperandAttr srcOffset;
    switch (opcode) {
    case D3D11_SB_OPCODE_GATHER4_PO:
    case D3DWDDM1_3_SB_OPCODE_GATHER4_PO_FEEDBACK:
    case D3D11_SB_OPCODE_GATHER4_PO_C:
    case D3DWDDM1_3_SB_OPCODE_GATHER4_PO_C_FEEDBACK: {
      auto offset = parseSrcOperand();
      FAILURE_IF_FAILED(offset);
      srcOffset = *offset;
    }
    }

    auto srcResource = parseSrcOperand();
    FAILURE_IF_FAILED(srcResource);

    auto srcSampler = parseSrcOperand();
    FAILURE_IF_FAILED(srcSampler);

    FailureOr<Instruction> instr;
    switch (opcode) {
    case D3D10_1_SB_OPCODE_GATHER4:
    case D3DWDDM1_3_SB_OPCODE_GATHER4_FEEDBACK: {
      if (feedback) {
        instr =
            builder.buildGather4Feedback(*dst, *srcAddress, *srcResource,
                                         *srcSampler, feedback, offset, loc);
      } else {
        instr = builder.buildGather4(*dst, *srcAddress, *srcResource,
                                     *srcSampler, offset, loc);
      }
      break;
    }
    case D3D11_SB_OPCODE_GATHER4_C:
    case D3DWDDM1_3_SB_OPCODE_GATHER4_C_FEEDBACK: {
      auto srcReferenceValue = parseSrcOperand();
      FAILURE_IF_FAILED(srcReferenceValue);
      if (feedback) {
        instr = builder.buildGather4CFeedback(*dst, *srcAddress, *srcResource,
                                              *srcSampler, *srcReferenceValue,
                                              feedback, offset, loc);
      } else {
        instr =
            builder.buildGather4C(*dst, *srcAddress, *srcResource, *srcSampler,
                                  *srcReferenceValue, offset, loc);
      }
      break;
    }
    case D3D11_SB_OPCODE_GATHER4_PO:
    case D3DWDDM1_3_SB_OPCODE_GATHER4_PO_FEEDBACK: {
      if (feedback) {
        instr = builder.buildGather4POFeedback(*dst, *srcAddress, srcOffset,
                                               *srcResource, *srcSampler,
                                               feedback, loc);
      } else {
        instr = builder.buildGather4PO(*dst, *srcAddress, srcOffset,
                                       *srcResource, *srcSampler, loc);
      }
      break;
    }
    case D3D11_SB_OPCODE_GATHER4_PO_C:
    case D3DWDDM1_3_SB_OPCODE_GATHER4_PO_C_FEEDBACK: {
      auto srcReferenceValue = parseSrcOperand();
      FAILURE_IF_FAILED(srcReferenceValue);
      if (feedback) {
        instr = builder.buildGather4POCFeedback(
            *dst, *srcAddress, srcOffset, *srcResource, *srcSampler,
            *srcReferenceValue, feedback, loc);

      } else {
        instr =
            builder.buildGather4POC(*dst, *srcAddress, srcOffset, *srcResource,
                                    *srcSampler, *srcReferenceValue, loc);
      }
      break;
    }
    default:
      llvm_unreachable("unhandled instruction");
    }

    FAILURE_IF_FAILED(instr);
    FAILURE_IF_FAILED(verifyInstructionLength(beginOffset, length));
    return instr;
  }

  FailureOr<Instruction> parseLdInstructions(uint32_t opcode,
                                             ExtendedInstruction &ext,
                                             size_t beginOffset,
                                             uint32_t length, Location loc) {
    dxsa::SampleOffsetAttr offset;
    if (ext.sampleOffset) {
      offset = builder.buildSampleOffsetAttr(*ext.sampleOffset);
    }

    auto dst = parseDstOperand();
    FAILURE_IF_FAILED(dst);

    dxsa::DstOperandAttr feedback;
    switch (opcode) {
    case D3DWDDM1_3_SB_OPCODE_LD_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_LD_MS_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_LD_RAW_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_LD_STRUCTURED_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_LD_UAV_TYPED_FEEDBACK:
      // For Feedback variant, feedback operand is the second dst
      // register.
      auto op = parseDstOperand();
      FAILURE_IF_FAILED(op);
      feedback = *op;
      break;
    }

    FailureOr<Instruction> instr;
    switch (opcode) {
    case D3D10_SB_OPCODE_LD:
    case D3DWDDM1_3_SB_OPCODE_LD_FEEDBACK: {
      auto srcAddress = parseSrcOperand();
      FAILURE_IF_FAILED(srcAddress);

      auto srcResource = parseSrcOperand();
      FAILURE_IF_FAILED(srcResource);

      if (feedback) {
        instr = builder.buildLdFeedback(*dst, *srcAddress, *srcResource,
                                        feedback, offset, loc);
      } else {
        instr = builder.buildLd(*dst, *srcAddress, *srcResource, offset, loc);
      }
      break;
    }
    case D3D10_SB_OPCODE_LD_MS:
    case D3DWDDM1_3_SB_OPCODE_LD_MS_FEEDBACK: {
      auto srcAddress = parseSrcOperand();
      FAILURE_IF_FAILED(srcAddress);

      auto srcResource = parseSrcOperand();
      FAILURE_IF_FAILED(srcResource);

      auto sampleIndex = parseSrcOperand();
      FAILURE_IF_FAILED(sampleIndex);

      if (feedback) {
        instr =
            builder.buildLd2dmsFeedback(*dst, *srcAddress, *srcResource,
                                        *sampleIndex, feedback, offset, loc);
      } else {
        instr = builder.buildLd2dms(*dst, *srcAddress, *srcResource,
                                    *sampleIndex, offset, loc);
      }
      break;
    }
    case D3D11_SB_OPCODE_LD_RAW:
    case D3DWDDM1_3_SB_OPCODE_LD_RAW_FEEDBACK: {
      auto srcByteOffset = parseSrcOperand();
      FAILURE_IF_FAILED(srcByteOffset);

      auto src = parseSrcOperand();
      FAILURE_IF_FAILED(src);

      if (feedback) {
        instr = builder.buildLdRawFeedback(*dst, *srcByteOffset, *src, feedback,
                                           loc);
      } else {
        instr = builder.buildLdRaw(*dst, *srcByteOffset, *src, loc);
      }
      break;
    }
    case D3D11_SB_OPCODE_LD_STRUCTURED:
    case D3DWDDM1_3_SB_OPCODE_LD_STRUCTURED_FEEDBACK: {
      auto srcAddress = parseSrcOperand();
      FAILURE_IF_FAILED(srcAddress);

      auto srcByteOffset = parseSrcOperand();
      FAILURE_IF_FAILED(srcByteOffset);

      auto src = parseSrcOperand();
      FAILURE_IF_FAILED(src);

      if (feedback) {
        instr = builder.buildLdStructuredFeedback(
            *dst, *srcAddress, *srcByteOffset, *src, feedback, loc);
      } else {
        instr = builder.buildLdStructured(*dst, *srcAddress, *srcByteOffset,
                                          *src, loc);
      }
      break;
    }
    case D3D11_SB_OPCODE_LD_UAV_TYPED:
    case D3DWDDM1_3_SB_OPCODE_LD_UAV_TYPED_FEEDBACK: {
      auto srcAddress = parseSrcOperand();
      FAILURE_IF_FAILED(srcAddress);

      auto srcUav = parseSrcOperand();
      FAILURE_IF_FAILED(srcUav);

      if (feedback) {
        instr = builder.buildLdUavTypedFeedback(*dst, *srcAddress, *srcUav,
                                                feedback, loc);
      } else {
        instr = builder.buildLdUavTyped(*dst, *srcAddress, *srcUav, loc);
      }
      break;
    }
    default:
      llvm_unreachable("unhandled instruction");
    }

    FAILURE_IF_FAILED(instr);
    FAILURE_IF_FAILED(verifyInstructionLength(beginOffset, length));
    return instr;
  }

  FailureOr<Instruction> parseDclInput(Location loc) {
    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    return builder.buildDclInput(*operand, loc);
  }

  FailureOr<Instruction> parseDclOutput(Location loc) {
    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    return builder.buildDclOutput(*operand, loc);
  }

  FailureOr<Instruction> parseDclIndexRange(Location loc) {
    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    auto count = parseToken();
    FAILURE_IF_FAILED(count);
    return builder.buildDclIndexRange(*operand, *count, loc);
  }

  FailureOr<Instruction> parseDclInputSiv(Location loc) {
    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    auto name = parseSystemValueName(getLocation());
    FAILURE_IF_FAILED(name);
    return builder.buildDclInputSiv(*operand, *name, loc);
  }

  FailureOr<Instruction> parseDclOutputSgv(Location loc) {
    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    auto name = parseSystemValueName(getLocation());
    FAILURE_IF_FAILED(name);
    return builder.buildDclOutputSgv(*operand, *name, loc);
  }

  FailureOr<Instruction> parseDclOutputSiv(Location loc) {
    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    auto name = parseSystemValueName(getLocation());
    FAILURE_IF_FAILED(name);
    return builder.buildDclOutputSiv(*operand, *name, loc);
  }

  FailureOr<Instruction> parseDclInputSgv(Location loc) {
    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    auto name = parseSystemValueName(getLocation());
    FAILURE_IF_FAILED(name);
    return builder.buildDclInputSgv(*operand, *name, loc);
  }

  FailureOr<Instruction> parseDclHsMaxTessFactor(Location loc) {
    auto token = parseToken();
    FAILURE_IF_FAILED(token);
    auto maxTessFactor = llvm::bit_cast<float>(*token);
    return builder.buildDclHsMaxTessFactor(maxTessFactor, loc);
  }

  FailureOr<Instruction> parseDclHsJoinPhaseInstanceCount(Location loc) {
    auto count = parseToken();
    FAILURE_IF_FAILED(count);
    return builder.buildDclHsJoinPhaseInstanceCount(*count, loc);
  }

  FailureOr<Instruction> parseDclHsForkPhaseInstanceCount(Location loc) {
    auto count = parseToken();
    FAILURE_IF_FAILED(count);
    return builder.buildDclHsForkPhaseInstanceCount(*count, loc);
  }

  FailureOr<Instruction> parseDclTgsmRaw(Location loc) {
    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    auto byteCount = parseToken();
    FAILURE_IF_FAILED(byteCount);
    return builder.buildDclTgsmRaw(*operand, *byteCount, loc);
  }

  FailureOr<Instruction> parseDclTgsmStructured(Location loc) {
    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    auto structByteStride = parseToken();
    FAILURE_IF_FAILED(structByteStride);
    auto structCount = parseToken();
    FAILURE_IF_FAILED(structCount);
    return builder.buildDclTgsmStructured(*operand, *structByteStride,
                                          *structCount, loc);
  }

  FailureOr<Instruction> parseDclImmediateConstantBuffer(uint32_t numTokens,
                                                         Location loc) {
    uint32_t numDataTokens = numTokens >= 2 ? numTokens - 2 : 0;

    auto dataTokens = parseTokens(numDataTokens);
    FAILURE_IF_FAILED(dataTokens);

    SmallVector<float, 16> values;
    values.reserve(numDataTokens);
    for (uint32_t token : *dataTokens)
      values.push_back(llvm::bit_cast<float>(token));

    return builder.buildDclImmediateConstantBuffer(values, loc);
  }

  FailureOr<Instruction> parseDclConstantBuffer(uint32_t opcodeToken,
                                                Location loc) {
    auto rawAccessPattern =
        DECODE_D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN(opcodeToken);
    auto accessPattern =
        dxsa::symbolizeConstantBufferAccessPattern(rawAccessPattern);
    if (!accessPattern)
      return emitError(loc, "unknown constant buffer access pattern: ")
             << rawAccessPattern;

    auto operandToken = parseToken();
    FAILURE_IF_FAILED(operandToken);

    auto operandType = DECODE_D3D10_SB_OPERAND_TYPE(*operandToken);
    if (operandType != D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER)
      return emitError(loc, "unexpected operand type: ") << operandType;

    if (DECODE_IS_D3D10_SB_OPERAND_EXTENDED(*operandToken))
      return emitError(loc, "extended operand tokens are not supported");

    auto indexDim = DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(*operandToken);
    if (indexDim != D3D10_SB_OPERAND_INDEX_2D &&
        indexDim != D3D10_SB_OPERAND_INDEX_3D)
      return emitError(loc, "unsupported index dimension: ") << indexDim;

    SmallVector<uint32_t, 3> indices;
    indices.reserve(indexDim);
    for (uint32_t i = 0; i < indexDim; ++i) {
      auto indexRepesentation =
          DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(i, *operandToken);
      if (indexRepesentation != D3D10_SB_OPERAND_INDEX_IMMEDIATE32)
        return emitError(loc, "unsupported index representation: ")
               << indexRepesentation;
      auto value = parseToken();
      FAILURE_IF_FAILED(value);
      indices.push_back(*value);
    }

    switch (indexDim) {
    case D3D10_SB_OPERAND_INDEX_2D:
      return builder.buildDclConstantBuffer(
          /*id=*/indices[0], /*size=*/indices[1], /*lbound=*/std::nullopt,
          /*ubound=*/std::nullopt, /*space=*/std::nullopt, *accessPattern, loc);
    case D3D10_SB_OPERAND_INDEX_3D: {
      auto sizeToken = parseToken();
      FAILURE_IF_FAILED(sizeToken);
      auto spaceToken = parseToken();
      FAILURE_IF_FAILED(spaceToken);
      return builder.buildDclConstantBuffer(
          /*id=*/indices[0], /*size=*/*sizeToken, /*lbound=*/indices[1],
          /*ubound=*/indices[2], /*space=*/*spaceToken, *accessPattern, loc);
    }
    default:
      llvm_unreachable("indexDim was validated above");
    }
  }

  FailureOr<Instruction> parseDclSampler(uint32_t opcodeToken, Location loc) {
    auto rawMode = DECODE_D3D10_SB_SAMPLER_MODE(opcodeToken);
    auto mode = dxsa::symbolizeSamplerMode(rawMode);
    if (!mode)
      return emitError(loc, "unknown sampler mode: ") << rawMode;

    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    if (operand->getType() != dxsa::OperandType::s)
      return emitError(loc, "operand must be a sampler register, got ")
             << dxsa::stringifyOperandType(operand->getType());
    auto indices = getRequiredImmIndices(*operand, loc);
    FAILURE_IF_FAILED(indices);
    auto indexDim = indices->size();
    if (indexDim != 1 && indexDim != 3)
      return emitError(loc, "operand must have a 1D or 3D index, got ")
             << indexDim;
    auto id = (*indices)[0];
    std::optional<uint32_t> lbound, ubound, space;
    if (indexDim == 3) {
      lbound = (*indices)[1];
      ubound = (*indices)[2];
      auto spaceToken = parseToken();
      FAILURE_IF_FAILED(spaceToken);
      space = *spaceToken;
    }

    return builder.buildDclSampler(id, lbound, ubound, space, *mode, loc);
  }

  FailureOr<dxsa::ResourceReturnType>
  parseResourceReturnType(uint32_t returnTypeToken, uint32_t component,
                          Location loc) {
    auto rawReturnType =
        DECODE_D3D10_SB_RESOURCE_RETURN_TYPE(returnTypeToken, component);
    auto returnType = dxsa::symbolizeResourceReturnType(rawReturnType);
    if (!returnType)
      return emitError(loc, "unknown resource return type: ") << rawReturnType;
    return *returnType;
  }

  FailureOr<Instruction> parseDclResource(uint32_t opcodeToken, Location loc) {
    auto rawDim = DECODE_D3D10_SB_RESOURCE_DIMENSION(opcodeToken);
    auto dim = dxsa::symbolizeResourceDimension(rawDim);
    if (!dim)
      return emitError(loc, "unknown resource dimension: ") << rawDim;

    std::optional<uint32_t> sampleCount;
    if (*dim == dxsa::ResourceDimension::texture2dms ||
        *dim == dxsa::ResourceDimension::texture2dmsarray) {
      auto rawSampleCount = DECODE_D3D10_SB_RESOURCE_SAMPLE_COUNT(opcodeToken);
      if (rawSampleCount == 0)
        return emitError(loc, "sample count must be non-zero for multisampled "
                              "dimension ")
               << dxsa::stringifyResourceDimension(*dim);
      sampleCount = rawSampleCount;
    }

    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    if (operand->getType() != dxsa::OperandType::t)
      return emitError(loc, "operand must be a resource register, got ")
             << dxsa::stringifyOperandType(operand->getType());
    auto indices = getRequiredImmIndices(*operand, loc);
    FAILURE_IF_FAILED(indices);
    auto indexDim = indices->size();
    if (indexDim != 1 && indexDim != 3)
      return emitError(loc, "operand must have a 1D or 3D index, got ")
             << indexDim;
    auto id = (*indices)[0];
    std::optional<uint32_t> lbound, ubound;
    if (indexDim == 3) {
      lbound = (*indices)[1];
      ubound = (*indices)[2];
    }

    auto returnTypeToken = parseToken();
    FAILURE_IF_FAILED(returnTypeToken);
    auto x = parseResourceReturnType(*returnTypeToken, 0, loc);
    FAILURE_IF_FAILED(x);
    auto y = parseResourceReturnType(*returnTypeToken, 1, loc);
    FAILURE_IF_FAILED(y);
    auto z = parseResourceReturnType(*returnTypeToken, 2, loc);
    FAILURE_IF_FAILED(z);
    auto w = parseResourceReturnType(*returnTypeToken, 3, loc);
    FAILURE_IF_FAILED(w);

    std::optional<uint32_t> space;
    if (indexDim == 3) {
      auto spaceToken = parseToken();
      FAILURE_IF_FAILED(spaceToken);
      space = *spaceToken;
    }

    return builder.buildDclResource(id, *dim, *x, *y, *z, *w, sampleCount,
                                    lbound, ubound, space, loc);
  }

  FailureOr<Instruction> parseDclResourceStructured(Location loc) {
    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    if (operand->getType() != dxsa::OperandType::t)
      return emitError(loc, "operand must be a resource register, got ")
             << dxsa::stringifyOperandType(operand->getType());
    auto indices = getRequiredImmIndices(*operand, loc);
    FAILURE_IF_FAILED(indices);
    auto indexDim = indices->size();
    if (indexDim != 1 && indexDim != 3)
      return emitError(loc, "operand must have a 1D or 3D index, got ")
             << indexDim;
    auto id = (*indices)[0];
    std::optional<uint32_t> lbound, ubound;
    if (indexDim == 3) {
      lbound = (*indices)[1];
      ubound = (*indices)[2];
    }

    auto strideToken = parseToken();
    FAILURE_IF_FAILED(strideToken);

    std::optional<uint32_t> space;
    if (indexDim == 3) {
      auto spaceToken = parseToken();
      FAILURE_IF_FAILED(spaceToken);
      space = *spaceToken;
    }

    return builder.buildDclResourceStructured(id, *strideToken, lbound, ubound,
                                              space, loc);
  }

  FailureOr<Instruction> parseDclResourceRaw(Location loc) {
    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    if (operand->getType() != dxsa::OperandType::t)
      return emitError(loc, "operand must be a resource register, got ")
             << dxsa::stringifyOperandType(operand->getType());
    auto indices = getRequiredImmIndices(*operand, loc);
    FAILURE_IF_FAILED(indices);
    auto indexDim = indices->size();
    if (indexDim != 1 && indexDim != 3)
      return emitError(loc, "operand must have a 1D or 3D index, got ")
             << indexDim;
    auto id = (*indices)[0];
    std::optional<uint32_t> lbound, ubound, space;
    if (indexDim == 3) {
      lbound = (*indices)[1];
      ubound = (*indices)[2];
      auto spaceToken = parseToken();
      FAILURE_IF_FAILED(spaceToken);
      space = *spaceToken;
    }

    return builder.buildDclResourceRaw(id, lbound, ubound, space, loc);
  }

  std::optional<dxsa::UAVFlags> decodeUavFlags(uint32_t opcodeToken) {
    auto flags = static_cast<dxsa::UAVFlags>(0);
    if (opcodeToken & D3D11_SB_GLOBALLY_COHERENT_ACCESS)
      flags = flags | dxsa::UAVFlags::globallyCoherent;
    if (opcodeToken & D3D11_SB_RASTERIZER_ORDERED_ACCESS)
      flags = flags | dxsa::UAVFlags::rasterizerOrdered;
    if (opcodeToken & D3D11_SB_UAV_HAS_ORDER_PRESERVING_COUNTER)
      flags = flags | dxsa::UAVFlags::hasOrderPreservingCounter;
    if (static_cast<uint32_t>(flags) == 0)
      return std::nullopt;
    return flags;
  }

  struct UavOperand {
    uint32_t id;
    std::optional<uint32_t> lbound;
    std::optional<uint32_t> ubound;
  };

  FailureOr<UavOperand> parseUavOperand(Location loc) {
    auto operand = parseDstOperand();
    FAILURE_IF_FAILED(operand);
    if (operand->getType() != dxsa::OperandType::u)
      return emitError(loc, "operand must be a uav register, got ")
             << dxsa::stringifyOperandType(operand->getType());
    auto indices = getRequiredImmIndices(*operand, loc);
    FAILURE_IF_FAILED(indices);
    auto indexDim = indices->size();
    if (indexDim != 1 && indexDim != 3)
      return emitError(loc, "operand must have a 1D or 3D index, got ")
             << indexDim;
    UavOperand uav{(*indices)[0], std::nullopt, std::nullopt};
    if (indexDim == 3) {
      uav.lbound = (*indices)[1];
      uav.ubound = (*indices)[2];
    }
    return uav;
  }

  FailureOr<std::optional<uint32_t>> parseUavSpace(const UavOperand &uav) {
    if (!uav.lbound)
      return std::optional<uint32_t>(std::nullopt);
    auto spaceToken = parseToken();
    FAILURE_IF_FAILED(spaceToken);
    return std::optional<uint32_t>(*spaceToken);
  }

  FailureOr<Instruction> parseDclUavTyped(uint32_t opcodeToken, Location loc) {
    auto rawDim = DECODE_D3D10_SB_RESOURCE_DIMENSION(opcodeToken);
    auto dim = dxsa::symbolizeResourceDimension(rawDim);
    if (!dim)
      return emitError(loc, "unknown resource dimension: ") << rawDim;

    auto flags = decodeUavFlags(opcodeToken);

    auto uav = parseUavOperand(loc);
    FAILURE_IF_FAILED(uav);

    auto returnTypeToken = parseToken();
    FAILURE_IF_FAILED(returnTypeToken);
    auto x = parseResourceReturnType(*returnTypeToken, 0, loc);
    FAILURE_IF_FAILED(x);
    auto y = parseResourceReturnType(*returnTypeToken, 1, loc);
    FAILURE_IF_FAILED(y);
    auto z = parseResourceReturnType(*returnTypeToken, 2, loc);
    FAILURE_IF_FAILED(z);
    auto w = parseResourceReturnType(*returnTypeToken, 3, loc);
    FAILURE_IF_FAILED(w);

    auto space = parseUavSpace(*uav);
    FAILURE_IF_FAILED(space);

    return builder.buildDclUavTyped(uav->id, *dim, *x, *y, *z, *w, flags,
                                    uav->lbound, uav->ubound, *space, loc);
  }

  FailureOr<Instruction> parseDclUavRaw(uint32_t opcodeToken, Location loc) {
    auto flags = decodeUavFlags(opcodeToken);
    auto uav = parseUavOperand(loc);
    FAILURE_IF_FAILED(uav);
    auto space = parseUavSpace(*uav);
    FAILURE_IF_FAILED(space);
    return builder.buildDclUavRaw(uav->id, flags, uav->lbound, uav->ubound,
                                  *space, loc);
  }

  FailureOr<Instruction> parseDclUavStructured(uint32_t opcodeToken,
                                               Location loc) {
    auto flags = decodeUavFlags(opcodeToken);
    auto uav = parseUavOperand(loc);
    FAILURE_IF_FAILED(uav);
    auto strideToken = parseToken();
    FAILURE_IF_FAILED(strideToken);
    auto space = parseUavSpace(*uav);
    FAILURE_IF_FAILED(space);
    return builder.buildDclUavStructured(uav->id, *strideToken, flags,
                                         uav->lbound, uav->ubound, *space, loc);
  }

  FailureOr<Instruction> parseDclIndexableTemp(Location loc) {
    auto id = parseToken();
    FAILURE_IF_FAILED(id);
    auto size = parseToken();
    FAILURE_IF_FAILED(size);
    auto numComponents = parseToken();
    FAILURE_IF_FAILED(numComponents);
    return builder.buildDclIndexableTemp(*id, *size, *numComponents, loc);
  }

  FailureOr<Instruction> parseDclThreadGroup(Location loc) {
    auto x = parseToken();
    FAILURE_IF_FAILED(x);
    auto y = parseToken();
    FAILURE_IF_FAILED(y);
    auto z = parseToken();
    FAILURE_IF_FAILED(z);
    return builder.buildDclThreadGroup(*x, *y, *z, loc);
  }

  OptionalParseResult parseDclInstruction(uint32_t opcodeToken, Location loc,
                                          Instruction &out) {
    FailureOr<Instruction> result;
    switch (DECODE_D3D10_SB_OPCODE_TYPE(opcodeToken)) {
    case D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS:
      result = parseDclGlobalFlags(opcodeToken, loc);
      break;
    case D3D10_SB_OPCODE_DCL_TEMPS:
      result = parseDclTemps(loc);
      break;
    case D3D11_SB_OPCODE_DCL_INPUT_CONTROL_POINT_COUNT:
      result = parseDclInputControlPointCount(opcodeToken, loc);
      break;
    case D3D11_SB_OPCODE_DCL_OUTPUT_CONTROL_POINT_COUNT:
      result = parseDclOutputControlPointCount(opcodeToken, loc);
      break;
    case D3D11_SB_OPCODE_DCL_TESS_DOMAIN:
      result = parseDclTessellatorDomain(opcodeToken, loc);
      break;
    case D3D11_SB_OPCODE_DCL_TESS_OUTPUT_PRIMITIVE:
      result = parseDclTessellatorOutputPrimitive(opcodeToken, loc);
      break;
    case D3D11_SB_OPCODE_DCL_TESS_PARTITIONING:
      result = parseDclTessellatorPartitioning(opcodeToken, loc);
      break;
    case D3D10_SB_OPCODE_DCL_GS_OUTPUT_PRIMITIVE_TOPOLOGY:
      result = parseDclOutputTopology(opcodeToken, loc);
      break;
    case D3D10_SB_OPCODE_DCL_GS_INPUT_PRIMITIVE:
      result = parseDclInputPrimitive(opcodeToken, loc);
      break;
    case D3D11_SB_OPCODE_DCL_GS_INSTANCE_COUNT:
      result = parseDclGsInstanceCount(loc);
      break;
    case D3D10_SB_OPCODE_DCL_MAX_OUTPUT_VERTEX_COUNT:
      result = parseDclMaxOutputVertexCount(loc);
      break;
    case D3D11_SB_OPCODE_DCL_STREAM:
      result = parseDclStream(loc);
      break;
    case D3D10_SB_OPCODE_DCL_INPUT_PS:
      result = parseDclInputPs(opcodeToken, loc);
      break;
    case D3D10_SB_OPCODE_DCL_INPUT_PS_SIV:
      result = parseDclInputPsSiv(opcodeToken, loc);
      break;
    case D3D10_SB_OPCODE_DCL_INPUT_PS_SGV:
      result = parseDclInputPsSgv(loc);
      break;
    case D3D10_SB_OPCODE_DCL_INPUT:
      result = parseDclInput(loc);
      break;
    case D3D10_SB_OPCODE_DCL_OUTPUT:
      result = parseDclOutput(loc);
      break;
    case D3D10_SB_OPCODE_DCL_INDEX_RANGE:
      result = parseDclIndexRange(loc);
      break;
    case D3D10_SB_OPCODE_DCL_INPUT_SGV:
      result = parseDclInputSgv(loc);
      break;
    case D3D10_SB_OPCODE_DCL_INPUT_SIV:
      result = parseDclInputSiv(loc);
      break;
    case D3D10_SB_OPCODE_DCL_OUTPUT_SGV:
      result = parseDclOutputSgv(loc);
      break;
    case D3D10_SB_OPCODE_DCL_OUTPUT_SIV:
      result = parseDclOutputSiv(loc);
      break;
    case D3D11_SB_OPCODE_DCL_HS_MAX_TESSFACTOR:
      result = parseDclHsMaxTessFactor(loc);
      break;
    case D3D11_SB_OPCODE_DCL_HS_JOIN_PHASE_INSTANCE_COUNT:
      result = parseDclHsJoinPhaseInstanceCount(loc);
      break;
    case D3D11_SB_OPCODE_DCL_HS_FORK_PHASE_INSTANCE_COUNT:
      result = parseDclHsForkPhaseInstanceCount(loc);
      break;
    case D3D11_SB_OPCODE_DCL_THREAD_GROUP_SHARED_MEMORY_RAW:
      result = parseDclTgsmRaw(loc);
      break;
    case D3D11_SB_OPCODE_DCL_THREAD_GROUP_SHARED_MEMORY_STRUCTURED:
      result = parseDclTgsmStructured(loc);
      break;
    case D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER:
      result = parseDclConstantBuffer(opcodeToken, loc);
      break;
    case D3D10_SB_OPCODE_DCL_SAMPLER:
      result = parseDclSampler(opcodeToken, loc);
      break;
    case D3D10_SB_OPCODE_DCL_RESOURCE:
      result = parseDclResource(opcodeToken, loc);
      break;
    case D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED:
      result = parseDclResourceStructured(loc);
      break;
    case D3D11_SB_OPCODE_DCL_RESOURCE_RAW:
      result = parseDclResourceRaw(loc);
      break;
    case D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_TYPED:
      result = parseDclUavTyped(opcodeToken, loc);
      break;
    case D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW:
      result = parseDclUavRaw(opcodeToken, loc);
      break;
    case D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_STRUCTURED:
      result = parseDclUavStructured(opcodeToken, loc);
      break;
    case D3D10_SB_OPCODE_DCL_INDEXABLE_TEMP:
      result = parseDclIndexableTemp(loc);
      break;
    case D3D11_SB_OPCODE_DCL_THREAD_GROUP:
      result = parseDclThreadGroup(loc);
      break;
    default:
      return std::nullopt;
    }
    if (failed(result))
      return failure();
    out = *result;
    return success();
  }

  FailureOr<Instruction> parseInstruction(uint32_t &instructionLengthInTokens) {
    auto beginOffset = currentTokenOffset;
    instructionLengthInTokens = 1; // Min instruction length
    auto opcodeToken0 = parseToken();
    if (failed(opcodeToken0))
      return failure();

    uint32_t opcode = DECODE_D3D10_SB_OPCODE_TYPE(*opcodeToken0);

    // CUSTOMDATA carries its total token count (>= 2) in token1.
    if (opcode == D3D10_SB_OPCODE_CUSTOMDATA) {
      auto numTokensToken = parseToken();
      FAILURE_IF_FAILED(numTokensToken);
      instructionLengthInTokens = std::max(*numTokensToken, 2u);

      if (failed(verifyInstructionLengthFitsBufferSize(
              beginOffset, instructionLengthInTokens)))
        return failure();

      auto customDataClass = DECODE_D3D10_SB_CUSTOMDATA_CLASS(*opcodeToken0);
      switch (customDataClass) {
      case D3D10_SB_CUSTOMDATA_DCL_IMMEDIATE_CONSTANT_BUFFER: {
        auto result =
            parseDclImmediateConstantBuffer(*numTokensToken, getLocation());
        FAILURE_IF_FAILED(result);
        if (failed(verifyInstructionLength(beginOffset,
                                           instructionLengthInTokens)))
          return failure();
        return *result;
      }
      default:
        return emitError(getLocation(), "customdata is not supported yet");
      }
    }

    instructionLengthInTokens = std::max(
        DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(*opcodeToken0), 1u);

    InstructionModifier modifier;
    modifier.preciseMask =
        DECODE_D3D11_SB_INSTRUCTION_PRECISE_VALUES(*opcodeToken0);
    modifier.saturate =
        DECODE_IS_D3D10_SB_INSTRUCTION_SATURATE_ENABLED(*opcodeToken0);

    ExtendedInstruction extendedInst;
    if (DECODE_IS_D3D10_SB_OPCODE_EXTENDED(*opcodeToken0)) {
      // opcodeToken0 is followed by zero or more opcodeToken1 that describe
      // sampler or resource parameters.
      Token opcodeToken1;
      do {
        opcodeToken1 = parseToken();
        FAILURE_IF_FAILED(opcodeToken1);
        parseExtendedInstruction(*opcodeToken1, extendedInst);
      } while (DECODE_IS_D3D10_SB_OPCODE_EXTENDED(*opcodeToken1));
    }

    // TODO: extended instructions:
    // BOOL b51PlusShader =
    // BOOL bExtended = DECODE_IS_D3D10_SB_OPCODE_EXTENDED(Token)
    // ...

    if (opcode >= D3D10_SB_NUM_OPCODES)
      return emitError(getLocation(), "unknown opcode: ") << opcode;

    if (failed(verifyInstructionLengthFitsBufferSize(
            beginOffset, instructionLengthInTokens)))
      return failure();

    Instruction dclInstruction;
    auto parseResult =
        parseDclInstruction(*opcodeToken0, getLocation(), dclInstruction);
    if (parseResult.has_value()) {
      FAILURE_IF_FAILED(*parseResult);
      if (failed(
              verifyInstructionLength(beginOffset, instructionLengthInTokens)))
        return failure();
      return dclInstruction;
    }

    unsigned numOperands = instrInfo[opcode].numOperands;

#define SATURABLE_OP(MNEMONIC, NUM_DST_OPERANDS, NUM_SRC_OPERANDS,             \
                     HAS_PRECISE)                                              \
  decodeOp<dxsa::MNEMONIC, dxsa::MNEMONIC##Sat, HAS_PRECISE, NUM_DST_OPERANDS, \
           NUM_SRC_OPERANDS>(beginOffset, instructionLengthInTokens, modifier, \
                             getLocation())
#define PLAIN_OP(MNEMONIC, NUM_DST_OPERANDS, NUM_SRC_OPERANDS, HAS_PRECISE)    \
  decodeOp<dxsa::MNEMONIC, dxsa::MNEMONIC, HAS_PRECISE, NUM_DST_OPERANDS,      \
           NUM_SRC_OPERANDS>(beginOffset, instructionLengthInTokens, modifier, \
                             getLocation())
#define STREAM_INDEX_OP(OP)                                                    \
  decodeStreamIndexOp<dxsa::OP>(beginOffset, instructionLengthInTokens,        \
                                getLocation())
// The `_z`/`_nz` control-flow pairs differ only in the test-boolean bit of
// their opcode token; both spellings decode to the same operand shape.
#define TEST_BOOLEAN_OP(MNEMONIC, NUM_DST_OPERANDS, NUM_SRC_OPERANDS)          \
  (DECODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(*opcodeToken0) ==                  \
           D3D10_SB_INSTRUCTION_TEST_NONZERO                                   \
       ? PLAIN_OP(MNEMONIC##Nz, NUM_DST_OPERANDS, NUM_SRC_OPERANDS,            \
                  HasPreciseAttr::No)                                          \
       : PLAIN_OP(MNEMONIC##Z, NUM_DST_OPERANDS, NUM_SRC_OPERANDS,             \
                  HasPreciseAttr::No))

    switch (opcode) {
    // Floating-point arithmetic instructions
    case D3D10_SB_OPCODE_ADD:
      return SATURABLE_OP(Add, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_DIV:
      return SATURABLE_OP(Div, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_DP2:
      return SATURABLE_OP(Dp2, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_DP3:
      return SATURABLE_OP(Dp3, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_DP4:
      return SATURABLE_OP(Dp4, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_EXP:
      return SATURABLE_OP(Exp, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_FRC:
      return SATURABLE_OP(Frc, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_LOG:
      return SATURABLE_OP(Log, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_MAD:
      return SATURABLE_OP(Mad, 1, 3, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_MAX:
      return SATURABLE_OP(Max, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_MIN:
      return SATURABLE_OP(Min, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_MUL:
      return SATURABLE_OP(Mul, 1, 2, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_RCP:
      return SATURABLE_OP(Rcp, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_ROUND_NE:
      return SATURABLE_OP(RoundNe, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_ROUND_NI:
      return SATURABLE_OP(RoundNi, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_ROUND_PI:
      return SATURABLE_OP(RoundPi, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_ROUND_Z:
      return SATURABLE_OP(RoundZ, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_RSQ:
      return SATURABLE_OP(Rsq, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_SINCOS:
      return SATURABLE_OP(Sincos, 2, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_SQRT:
      return SATURABLE_OP(Sqrt, 1, 1, HasPreciseAttr::Yes);
    // Move instructions
    case D3D10_SB_OPCODE_MOV:
      return SATURABLE_OP(Mov, 1, 1, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_DMOV:
      return SATURABLE_OP(DMov, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_MOVC:
      return SATURABLE_OP(MovC, 1, 3, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_DMOVC:
      return SATURABLE_OP(DMovC, 1, 3, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_SWAPC:
      return PLAIN_OP(SwapC, 2, 3, HasPreciseAttr::Yes);
    // Type conversion instructions
    case D3D11_SB_OPCODE_DTOF:
      return PLAIN_OP(DToF, 1, 1, HasPreciseAttr::Yes);
    case D3D11_1_SB_OPCODE_DTOI:
      return PLAIN_OP(DToI, 1, 1, HasPreciseAttr::Yes);
    case D3D11_1_SB_OPCODE_DTOU:
      return PLAIN_OP(DToU, 1, 1, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_F16TOF32:
      return PLAIN_OP(F16ToF32, 1, 1, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_F32TOF16:
      return PLAIN_OP(F32ToF16, 1, 1, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_FTOD:
      return PLAIN_OP(FToD, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_FTOI:
      return PLAIN_OP(FToI, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_FTOU:
      return PLAIN_OP(FToU, 1, 1, HasPreciseAttr::Yes);
    case D3D11_1_SB_OPCODE_ITOD:
      return PLAIN_OP(IToD, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_ITOF:
      return PLAIN_OP(IToF, 1, 1, HasPreciseAttr::Yes);
    case D3D11_1_SB_OPCODE_UTOD:
      return PLAIN_OP(UToD, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_UTOF:
      return PLAIN_OP(UToF, 1, 1, HasPreciseAttr::Yes);
    // Comparison instructions
    case D3D10_SB_OPCODE_EQ:
      return PLAIN_OP(Eq, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_GE:
      return PLAIN_OP(Ge, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_LT:
      return PLAIN_OP(Lt, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_NE:
      return PLAIN_OP(Ne, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_IEQ:
      return PLAIN_OP(Ieq, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_IGE:
      return PLAIN_OP(Ige, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_ILT:
      return PLAIN_OP(Ilt, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_INE:
      return PLAIN_OP(Ine, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_UGE:
      return PLAIN_OP(Uge, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_ULT:
      return PLAIN_OP(Ult, 1, 2, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_DEQ:
      return PLAIN_OP(Deq, 1, 2, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_DGE:
      return PLAIN_OP(Dge, 1, 2, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_DLT:
      return PLAIN_OP(Dlt, 1, 2, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_DNE:
      return PLAIN_OP(Dne, 1, 2, HasPreciseAttr::Yes);
    // Integer arithmetic instructions
    case D3D10_SB_OPCODE_IADD:
      return PLAIN_OP(IAdd, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_IMAX:
      return PLAIN_OP(IMax, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_IMIN:
      return PLAIN_OP(IMin, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_INEG:
      return PLAIN_OP(INeg, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_UMAX:
      return PLAIN_OP(UMax, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_UMIN:
      return PLAIN_OP(UMin, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_IMAD:
      return PLAIN_OP(Imad, 1, 3, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_IMUL:
      return PLAIN_OP(Imul, 2, 2, HasPreciseAttr::Yes);
    case D3D11_1_SB_OPCODE_MSAD:
      return PLAIN_OP(Msad, 1, 3, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_UDIV:
      return PLAIN_OP(UDiv, 2, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_UMUL:
      return PLAIN_OP(UMul, 2, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_UMAD:
      return PLAIN_OP(UMad, 1, 3, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_UADDC:
      return PLAIN_OP(UAddc, 2, 2, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_USUBB:
      return PLAIN_OP(USubb, 2, 2, HasPreciseAttr::Yes);
    // Bitwise instructions
    case D3D10_SB_OPCODE_AND:
      return PLAIN_OP(And, 1, 2, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_BFREV:
      return PLAIN_OP(BFRev, 1, 1, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_COUNTBITS:
      return PLAIN_OP(CountBits, 1, 1, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_FIRSTBIT_LO:
      return PLAIN_OP(FirstBitLo, 1, 1, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_FIRSTBIT_HI:
      return PLAIN_OP(FirstBitHi, 1, 1, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_FIRSTBIT_SHI:
      return PLAIN_OP(FirstBitSHi, 1, 1, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_IBFE:
      return PLAIN_OP(IBFE, 1, 3, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_ISHL:
      return PLAIN_OP(IShl, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_ISHR:
      return PLAIN_OP(IShr, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_NOT:
      return PLAIN_OP(Not, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_OR:
      return PLAIN_OP(Or, 1, 2, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_UBFE:
      return PLAIN_OP(UBFE, 1, 3, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_USHR:
      return PLAIN_OP(UShr, 1, 2, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_XOR:
      return PLAIN_OP(Xor, 1, 2, HasPreciseAttr::Yes);
    // Raster instructions
    case D3D10_SB_OPCODE_DERIV_RTX:
      return SATURABLE_OP(DerivRtx, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_DERIV_RTY:
      return SATURABLE_OP(DerivRty, 1, 1, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_DERIV_RTX_COARSE:
      return SATURABLE_OP(DerivRtxCoarse, 1, 1, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_DERIV_RTY_COARSE:
      return SATURABLE_OP(DerivRtyCoarse, 1, 1, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_DERIV_RTX_FINE:
      return SATURABLE_OP(DerivRtxFine, 1, 1, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_DERIV_RTY_FINE:
      return SATURABLE_OP(DerivRtyFine, 1, 1, HasPreciseAttr::Yes);
    case D3D10_1_SB_OPCODE_LOD:
      return PLAIN_OP(LOD, 1, 3, HasPreciseAttr::Yes);
    // Atomic instructions
    case D3D11_SB_OPCODE_ATOMIC_AND:
      return PLAIN_OP(AtomicAnd, 1, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_ATOMIC_OR:
      return PLAIN_OP(AtomicOr, 1, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_ATOMIC_XOR:
      return PLAIN_OP(AtomicXor, 1, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_ATOMIC_IADD:
      return PLAIN_OP(AtomicIAdd, 1, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_ATOMIC_IMAX:
      return PLAIN_OP(AtomicIMax, 1, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_ATOMIC_IMIN:
      return PLAIN_OP(AtomicIMin, 1, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_ATOMIC_UMAX:
      return PLAIN_OP(AtomicUMax, 1, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_ATOMIC_UMIN:
      return PLAIN_OP(AtomicUMin, 1, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_ATOMIC_CMP_STORE:
      return PLAIN_OP(AtomicCmpStore, 1, 3, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_IMM_ATOMIC_IADD:
      return PLAIN_OP(ImmAtomicIAdd, 2, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_IMM_ATOMIC_AND:
      return PLAIN_OP(ImmAtomicAnd, 2, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_IMM_ATOMIC_OR:
      return PLAIN_OP(ImmAtomicOr, 2, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_IMM_ATOMIC_XOR:
      return PLAIN_OP(ImmAtomicXor, 2, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_IMM_ATOMIC_EXCH:
      return PLAIN_OP(ImmAtomicExch, 2, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_IMM_ATOMIC_CMP_EXCH:
      return PLAIN_OP(ImmAtomicCmpExch, 2, 3, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_IMM_ATOMIC_IMAX:
      return PLAIN_OP(ImmAtomicIMax, 2, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_IMM_ATOMIC_IMIN:
      return PLAIN_OP(ImmAtomicIMin, 2, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_IMM_ATOMIC_UMAX:
      return PLAIN_OP(ImmAtomicUMax, 2, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_IMM_ATOMIC_UMIN:
      return PLAIN_OP(ImmAtomicUMin, 2, 2, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_IMM_ATOMIC_ALLOC:
      return PLAIN_OP(ImmAtomicAlloc, 1, 1, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_IMM_ATOMIC_CONSUME:
      return PLAIN_OP(ImmAtomicConsume, 1, 1, HasPreciseAttr::No);
    // Topology instructions
    case D3D10_SB_OPCODE_EMIT:
      return PLAIN_OP(Emit, 0, 0, HasPreciseAttr::No);
    case D3D10_SB_OPCODE_EMITTHENCUT:
      return PLAIN_OP(EmitThenCut, 0, 0, HasPreciseAttr::No);
    case D3D10_SB_OPCODE_CUT:
      return PLAIN_OP(Cut, 0, 0, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_EMIT_STREAM:
      return STREAM_INDEX_OP(EmitStream);
    case D3D11_SB_OPCODE_CUT_STREAM:
      return STREAM_INDEX_OP(CutStream);
    case D3D11_SB_OPCODE_EMITTHENCUT_STREAM:
      return STREAM_INDEX_OP(EmitThenCutStream);
    // Resource instructions
    case D3D11_SB_OPCODE_BUFINFO:
      return PLAIN_OP(BufInfo, 1, 1, HasPreciseAttr::Yes);
    case D3D10_SB_OPCODE_RESINFO: {
      D3D10_SB_RESINFO_INSTRUCTION_RETURN_TYPE resinfo_type =
          DECODE_D3D10_SB_RESINFO_INSTRUCTION_RETURN_TYPE(*opcodeToken0);
      switch (resinfo_type) {
      case D3D10_SB_RESINFO_INSTRUCTION_RETURN_FLOAT:
        return PLAIN_OP(ResInfo, 1, 2, HasPreciseAttr::Yes);
      case D3D10_SB_RESINFO_INSTRUCTION_RETURN_RCPFLOAT:
        return PLAIN_OP(ResInfoRcpFloat, 1, 2, HasPreciseAttr::Yes);
      case D3D10_SB_RESINFO_INSTRUCTION_RETURN_UINT:
        return PLAIN_OP(ResInfoUInt, 1, 2, HasPreciseAttr::Yes);
      }
      llvm_unreachable("unhandled resinfo");
    }
    case D3D10_1_SB_OPCODE_SAMPLE_INFO: {
      D3D10_SB_INSTRUCTION_RETURN_TYPE sampleinfo_type =
          DECODE_D3D10_SB_INSTRUCTION_RETURN_TYPE(*opcodeToken0);
      switch (sampleinfo_type) {
      case D3D10_SB_INSTRUCTION_RETURN_FLOAT:
        return PLAIN_OP(SampleInfo, 1, 1, HasPreciseAttr::Yes);
      case D3D10_SB_INSTRUCTION_RETURN_UINT:
        return PLAIN_OP(SampleInfoUInt, 1, 1, HasPreciseAttr::Yes);
      }
      llvm_unreachable("unhandled sampleinfo");
    }
    case D3D10_1_SB_OPCODE_SAMPLE_POS:
      return PLAIN_OP(SamplePos, 1, 2, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_EVAL_CENTROID:
      return PLAIN_OP(EvalCentroid, 1, 1, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_EVAL_SAMPLE_INDEX:
      return PLAIN_OP(EvalSampleIndex, 1, 2, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_EVAL_SNAPPED:
      return PLAIN_OP(EvalSnapped, 1, 2, HasPreciseAttr::Yes);
    // Double-precision arithmetic instructions
    case D3D11_SB_OPCODE_DADD:
      return SATURABLE_OP(DAdd, 1, 2, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_DMAX:
      return SATURABLE_OP(DMax, 1, 2, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_DMIN:
      return SATURABLE_OP(DMin, 1, 2, HasPreciseAttr::Yes);
    case D3D11_SB_OPCODE_DMUL:
      return SATURABLE_OP(DMul, 1, 2, HasPreciseAttr::Yes);
    case D3D11_1_SB_OPCODE_DDIV:
      return SATURABLE_OP(DDiv, 1, 2, HasPreciseAttr::Yes);
    case D3D11_1_SB_OPCODE_DFMA:
      return SATURABLE_OP(DFma, 1, 3, HasPreciseAttr::Yes);
    case D3D11_1_SB_OPCODE_DRCP:
      return SATURABLE_OP(DRcp, 1, 1, HasPreciseAttr::Yes);
    // Control flow instructions
    case D3D11_SB_OPCODE_ABORT:
      return PLAIN_OP(Abort, 0, 0, HasPreciseAttr::No);
    case D3D10_SB_OPCODE_BREAK:
      return PLAIN_OP(Break, 0, 0, HasPreciseAttr::No);
    case D3D10_SB_OPCODE_CALL:
      return PLAIN_OP(Call, 0, 1, HasPreciseAttr::No);
    case D3D10_SB_OPCODE_CALLC:
      if (DECODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(*opcodeToken0) ==
          D3D10_SB_INSTRUCTION_TEST_NONZERO)
        return PLAIN_OP(CallcNz, 0, 2, HasPreciseAttr::No);
      return PLAIN_OP(CallcZ, 0, 2, HasPreciseAttr::No);
    case D3D10_SB_OPCODE_BREAKC:
      return TEST_BOOLEAN_OP(Breakc, 0, 1);
    case D3D10_SB_OPCODE_CASE:
      return PLAIN_OP(Case, 0, 1, HasPreciseAttr::No);
    case D3D10_SB_OPCODE_CONTINUEC:
      return TEST_BOOLEAN_OP(Continuec, 0, 1);
    case D3D10_SB_OPCODE_DISCARD:
      return TEST_BOOLEAN_OP(Discard, 0, 1);
    case D3D10_SB_OPCODE_IF:
      return TEST_BOOLEAN_OP(If, 0, 1);
    case D3D10_SB_OPCODE_RETC:
      return TEST_BOOLEAN_OP(Retc, 0, 1);
    case D3D10_SB_OPCODE_SWITCH:
      return PLAIN_OP(Switch, 0, 1, HasPreciseAttr::No);
    case D3D10_SB_OPCODE_CONTINUE:
      return PLAIN_OP(Continue, 0, 0, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_DEBUG_BREAK:
      return PLAIN_OP(DebugBreak, 0, 0, HasPreciseAttr::No);
    case D3D10_SB_OPCODE_DEFAULT:
      return PLAIN_OP(Default, 0, 0, HasPreciseAttr::No);
    case D3D10_SB_OPCODE_ELSE:
      return PLAIN_OP(Else, 0, 0, HasPreciseAttr::No);
    case D3D10_SB_OPCODE_ENDIF:
      return PLAIN_OP(Endif, 0, 0, HasPreciseAttr::No);
    case D3D10_SB_OPCODE_ENDLOOP:
      return PLAIN_OP(Endloop, 0, 0, HasPreciseAttr::No);
    case D3D10_SB_OPCODE_ENDSWITCH:
      return PLAIN_OP(Endswitch, 0, 0, HasPreciseAttr::No);
    case D3D10_SB_OPCODE_LABEL:
      return PLAIN_OP(Label, 1, 0, HasPreciseAttr::No);
    case D3D10_SB_OPCODE_LOOP:
      return PLAIN_OP(Loop, 0, 0, HasPreciseAttr::No);
    case D3D10_SB_OPCODE_RET:
      return PLAIN_OP(Ret, 0, 0, HasPreciseAttr::No);
    // Shader phase instructions
    case D3D11_SB_OPCODE_HS_DECLS:
      return PLAIN_OP(HsDecls, 0, 0, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_HS_CONTROL_POINT_PHASE:
      return PLAIN_OP(HsControlPointPhase, 0, 0, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_HS_FORK_PHASE:
      return PLAIN_OP(HsForkPhase, 0, 0, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_HS_JOIN_PHASE:
      return PLAIN_OP(HsJoinPhase, 0, 0, HasPreciseAttr::No);
    // Other instructions
    case D3D10_SB_OPCODE_NOP:
      return PLAIN_OP(Nop, 0, 0, HasPreciseAttr::No);
    case D3D11_SB_OPCODE_SYNC:
      return parseSync(*opcodeToken0, beginOffset, instructionLengthInTokens,
                       getLocation());
    case D3D10_SB_OPCODE_SAMPLE:
    case D3D10_SB_OPCODE_SAMPLE_B:
    case D3D10_SB_OPCODE_SAMPLE_C:
    case D3D10_SB_OPCODE_SAMPLE_C_LZ:
    case D3D10_SB_OPCODE_SAMPLE_D:
    case D3D10_SB_OPCODE_SAMPLE_L:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_CLAMP_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_B_CLAMP_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_C_CLAMP_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_C_LZ_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_D_CLAMP_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_SAMPLE_L_FEEDBACK:
      return parseSampleInstruction(opcode, extendedInst, beginOffset,
                                    instructionLengthInTokens, getLocation());
    case D3D10_1_SB_OPCODE_GATHER4:
    case D3D11_SB_OPCODE_GATHER4_C:
    case D3D11_SB_OPCODE_GATHER4_PO:
    case D3D11_SB_OPCODE_GATHER4_PO_C:
    case D3DWDDM1_3_SB_OPCODE_GATHER4_C_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_GATHER4_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_GATHER4_PO_C_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_GATHER4_PO_FEEDBACK:
      return parseGather4Instructions(opcode, extendedInst, beginOffset,
                                      instructionLengthInTokens, getLocation());
    case D3D10_SB_OPCODE_LD:
    case D3D10_SB_OPCODE_LD_MS:
    case D3D11_SB_OPCODE_LD_RAW:
    case D3D11_SB_OPCODE_LD_STRUCTURED:
    case D3D11_SB_OPCODE_LD_UAV_TYPED:
    case D3DWDDM1_3_SB_OPCODE_LD_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_LD_MS_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_LD_RAW_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_LD_STRUCTURED_FEEDBACK:
    case D3DWDDM1_3_SB_OPCODE_LD_UAV_TYPED_FEEDBACK:
      return parseLdInstructions(opcode, extendedInst, beginOffset,
                                 instructionLengthInTokens, getLocation());
    }
#undef SATURABLE_OP
#undef PLAIN_OP
#undef STREAM_INDEX_OP
#undef TEST_BOOLEAN_OP

    SmallVector<Operand, 8> operands;
    for (unsigned i = 0; i < numOperands; ++i) {
      FailureOr<Operand> operand = parseOperand();
      if (failed(operand))
        return failure();
      operands.push_back(*operand);
    }

    if (failed(verifyInstructionLength(beginOffset, instructionLengthInTokens)))
      return failure();

    return builder.buildInstruction(instrInfo[opcode].name, operands, modifier,
                                    getLocation());
  }

  /// On failure, reports the declared token length for the unknown fallback
  /// and the first nested diagnostic that explains the failure.
  bool tryParseInstructionOrRewind(uint32_t &instructionLengthInTokens,
                                   std::string &errorMessage) {
    auto numOpsBefore = builder.getNumOps();

    // Scope for ScopedDiagnosticHandler
    {
      ScopedDiagnosticHandler capture(name.getContext(), [&](Diagnostic &d) {
        if (errorMessage.empty() &&
            d.getSeverity() == DiagnosticSeverity::Error)
          errorMessage = d.str();
        return success();
      });
      if (succeeded(parseInstruction(instructionLengthInTokens)))
        return true;
    }

    builder.rewindOpsTo(numOpsBefore);
    return false;
  }

  LogicalResult parseUnknownTokens(uint32_t numTokens) {
    auto loc = getLocation();
    numTokens = std::min<uint32_t>(numTokens, getRemainingBytes() / tokenSize);
    auto tokens = parseTokens(numTokens);
    FAILURE_IF_FAILED(tokens);
    builder.buildUnknown(*tokens, loc);
    return success();
  }

  LogicalResult parseNextInstruction() {
    auto beginOffset = currentTokenOffset;
    uint32_t instructionLengthInTokens = 0;
    std::string errorMessage;
    if (tryParseInstructionOrRewind(instructionLengthInTokens, errorMessage))
      return success();

    currentTokenOffset = beginOffset;
    emitWarning(getLocation())
        << "treating next " << instructionLengthInTokens
        << " token(s) as unknown"
        << (errorMessage.empty() ? "" : ": " + errorMessage);
    return parseUnknownTokens(instructionLengthInTokens);
  }

  FailureOr<Module> parseModule() {
    FileLineColLoc loc = getLocation(0);
    auto header = parseProgramHeader();
    FAILURE_IF_FAILED(header);
    dxsa::ProgramTypeAttr programType;
    std::optional<uint32_t> majorVersion;
    std::optional<uint32_t> minorVersion;
    if (*header) {
      programType =
          dxsa::ProgramTypeAttr::get(name.getContext(), (*header)->type);
      majorVersion = (*header)->major;
      minorVersion = (*header)->minor;
    }
    auto module =
        builder.createModule(programType, majorVersion, minorVersion, loc);
    while (getRemainingBytes() >= tokenSize) {
      if (failed(parseNextInstruction()))
        return failure();
    }
    if (auto trailingBytes = getRemainingBytes())
      emitWarning(getLocation(0))
          << "ignoring " << trailingBytes << " trailing byte(s)";
    return module;
  }

  struct ProgramHeader {
    dxsa::ProgramType type;
    uint8_t major;
    uint8_t minor;
  };

  /// If the buffer begins with a tokenized-program header (VersionToken +
  /// LengthToken), decode and consume both tokens and return the program type
  /// and shader version. Otherwise return without touching the parser current
  /// position.
  FailureOr<std::optional<ProgramHeader>> parseProgramHeader() {
    auto remainingBytes = getRemainingBytes();
    if (remainingBytes < tokenSize)
      return std::optional<ProgramHeader>{};

    auto versionToken = support::endian::read<uint32_t>(
        buffer.begin() + currentTokenOffset, endianness::little);
    uint32_t rawProgramType =
        DECODE_D3D10_SB_TOKENIZED_PROGRAM_TYPE(versionToken);
    auto programType = dxsa::symbolizeProgramType(rawProgramType);
    if (!programType)
      return std::optional<ProgramHeader>{};

    constexpr size_t headerSize = 2 * tokenSize;
    if (remainingBytes < headerSize)
      return emitError(getLocation(),
                       "expected LengthToken after VersionToken");

    auto versionTokenLength =
        DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(versionToken);
    if (versionTokenLength != 0)
      return emitError(getLocation(), "VersionToken length must be 0, got ")
             << versionTokenLength;

    auto lengthToken = support::endian::read<uint32_t>(
        buffer.begin() + currentTokenOffset + tokenSize, endianness::little);
    auto programLength = DECODE_D3D10_SB_TOKENIZED_PROGRAM_LENGTH(lengthToken);
    constexpr size_t minProgramLen = 2; // VersionToken and LengthToken
    if (programLength < minProgramLen)
      return emitError(getLocation(), "LengthToken must be >= ")
             << minProgramLen << ", got " << programLength;

    uint8_t major =
        DECODE_D3D10_SB_TOKENIZED_PROGRAM_MAJOR_VERSION(versionToken);
    uint8_t minor =
        DECODE_D3D10_SB_TOKENIZED_PROGRAM_MINOR_VERSION(versionToken);

    FAILURE_IF_FAILED(parseToken()); // VersionToken
    FAILURE_IF_FAILED(parseToken()); // LengthToken
    return std::optional<ProgramHeader>{{*programType, major, minor}};
  }

  LogicalResult verifyInstructionLengthFitsBufferSize(size_t beginOffset,
                                                      uint32_t length) {
    if (beginOffset + static_cast<size_t>(length) * tokenSize > buffer.size())
      return emitError(getLocation(),
                       "instruction length exceeds program size");
    return success();
  }

  LogicalResult verifyInstructionLength(size_t beginOffset, uint32_t length) {
    // WORKAROUND: some instructions such as samplepos have trailing
    // tokens that do not correspond to any operands. Therefore we
    // allow actual instruction length to be less then length read
    // from the opcode token.
    if (((currentTokenOffset - beginOffset) / tokenSize) > length) {
      emitError(getLocation(), "operands did not fit into instruction length");
      return failure();
    }
    // WORKAROUND: skip unparsed tokens in the end. See above.
    while (((currentTokenOffset - beginOffset) / tokenSize) < length) {
      auto token = parseToken();
      FAILURE_IF_FAILED(token);
    }
    return success();
  }

private:
  DXBuilder &builder;
  StringAttr name;
  StringRef buffer;
  size_t currentTokenOffset{0};
  InstructionInfo instrInfo[D3D10_SB_NUM_OPCODES];
};

namespace feme::dxsa {
static OwningOpRef<ModuleOp> parseProgram(StringRef buffer, StringAttr name,
                                          MLIRContext *context) {
  // FIXME:
  context->allowUnregisteredDialects();
  context->loadAllAvailableDialects();

  DXBuilder builder(context);
  Parser parser(builder, name, buffer);
  FailureOr<ModuleOp> mod = parser.parseModule();
  if (failed(mod))
    return {nullptr};
  return {*mod};
}

OwningOpRef<ModuleOp> deserialize(llvm::SourceMgr &source,
                                  MLIRContext *context) {
  if (source.getNumBuffers() != 1) {
    emitError(UnknownLoc::get(context), "one source file should be provided");
    return nullptr;
  }

  const auto *memBuffer = source.getMemoryBuffer(source.getMainFileID());
  return parseProgram(
      memBuffer->getBuffer(),
      StringAttr::get(context, memBuffer->getBufferIdentifier()), context);
}

} // namespace feme::dxsa
