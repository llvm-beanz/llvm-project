set(LLVM_LINK_COMPONENTS
  AsmParser
  Core
  Support
  TargetParser
  TestingSupport
  )

add_feme_unittest(FeMeTransformsDXILTests
  SignatureImportTest.cpp
  )
target_link_libraries(FeMeTransformsDXILTests
  PRIVATE
  FeMeCore
  FeMeTransformsDXIL
  )
