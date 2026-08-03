set(LLVM_LINK_COMPONENTS
  Support
  )

add_feme_unittest(FeMeDXBCAssemblerTests
  InstructionTest.cpp
  LexerTest.cpp
  ParserTest.cpp
  )
target_link_libraries(FeMeDXBCAssemblerTests
  PRIVATE
  FeMeDXBCAssembler
  )
