set(LLVM_LINK_COMPONENTS
  BinaryFormat
  Core
  Object
  ObjectYAML
  Support
  TestingSupport
  )

add_feme_unittest(FeMeImportDXBCTests
  DXBCImporterTest.cpp
  )
target_link_libraries(FeMeImportDXBCTests
  PRIVATE
  FeMeCore
  FeMeImportDXBC
  )
