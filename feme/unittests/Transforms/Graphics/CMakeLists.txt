set(LLVM_LINK_COMPONENTS
  AsmParser
  Core
  Support
  TargetParser
  TestingSupport
  )

add_feme_unittest(FeMeTransformsGraphicsTests
  CanonicalizeStageTest.cpp
  ValidateStageTest.cpp
  )
target_link_libraries(FeMeTransformsGraphicsTests
  PRIVATE
  FeMeCore
  FeMeTransformsDXIL
  FeMeTransformsGraphics
  )
