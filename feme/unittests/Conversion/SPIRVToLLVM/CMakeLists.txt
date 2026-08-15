set(LLVM_LINK_COMPONENTS
  Support
  TargetParser
  TestingSupport
  )

add_feme_unittest(FeMeConversionSPIRVToLLVMTests
  SPIRVToLLVMTest.cpp
  )
target_link_libraries(FeMeConversionSPIRVToLLVMTests
  PRIVATE
  FeMeConversionSPIRVToLLVM
  MLIRIR
  MLIRParser
  MLIRSPIRVDialect
  )
