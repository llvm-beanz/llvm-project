# AsmParser
add_llvm_library(DXILAsmParser
  LLLexer.cpp
  LLParser.cpp
  Parser.cpp

  ADDITIONAL_HEADER_DIRS
  ${DXIL_TOOLS_SOURCE_DIR}/include/dxil/AsmParser

  DEPENDS
  dxil_intrinsics_gen

  LINK_LIBS
  DXILCore

  LINK_COMPONENTS
  Support
  )
