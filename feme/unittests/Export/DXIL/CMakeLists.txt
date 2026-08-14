set(LLVM_LINK_COMPONENTS
  Core
  Support
  TargetParser
  TestingSupport
  )

add_feme_unittest(FeMeExportDXILTests
  DXILExporterTest.cpp
  )
target_link_libraries(FeMeExportDXILTests
  PRIVATE
  FeMeCore
  FeMeExportDXIL
  )
