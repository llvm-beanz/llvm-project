set(LLVM_LINK_COMPONENTS
  Core
  Support
  TargetParser
  TestingSupport
  )

add_feme_unittest(FeMeExportSPIRVTests
  SPIRVExporterTest.cpp
  )
target_link_libraries(FeMeExportSPIRVTests
  PRIVATE
  FeMeCore
  FeMeExportSPIRV
  )
