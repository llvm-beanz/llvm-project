add_llvm_library(FeMeDXBCAssembler
  Lexer.cpp
  Parser.cpp
  Instruction.cpp
  Encoder.cpp

  LINK_COMPONENTS
  Support
  BinaryFormat
  )
