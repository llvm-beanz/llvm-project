add_llvm_library(FeMeDXBCAssembler
  Lexer.cpp
  Parser.cpp
  Instruction.cpp

  LINK_COMPONENTS
  Support
  BinaryFormat
  )
