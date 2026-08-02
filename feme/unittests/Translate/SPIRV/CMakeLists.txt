set(LLVM_LINK_COMPONENTS
  Support
  TestingSupport
  )

add_feme_unittest(FeMeTranslateSPIRVTests
  SPIRVToLLVMTranslatorTest.cpp
  )
target_link_libraries(FeMeTranslateSPIRVTests
  PRIVATE
  FeMeCore
  FeMeTranslateSPIRV
  MLIRIR
  MLIRParser
  MLIRSPIRVDialect
  )
