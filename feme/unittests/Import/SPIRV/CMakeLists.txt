set(LLVM_LINK_COMPONENTS
  Support
  TestingSupport
  )

add_feme_unittest(FeMeImportSPIRVTests
  SPIRVImporterTest.cpp
  )
target_link_libraries(FeMeImportSPIRVTests
  PRIVATE
  FeMeCore
  FeMeImportSPIRV
  MLIRIR
  MLIRParser
  MLIRSPIRVDialect
  MLIRSPIRVSerialization
  )
