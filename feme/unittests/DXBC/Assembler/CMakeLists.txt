set(LLVM_LINK_COMPONENTS
  BinaryFormat
  Support
  )

add_feme_unittest(FeMeDXBCAssemblerTests
  InstructionTest.cpp
  LexerTest.cpp
  ParserTest.cpp
  EncoderTest.cpp
  )
target_link_libraries(FeMeDXBCAssemblerTests
  PRIVATE
  FeMeDXBCAssembler
  )
