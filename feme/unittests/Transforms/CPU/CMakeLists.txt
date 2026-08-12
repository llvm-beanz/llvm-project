set(LLVM_LINK_COMPONENTS
  AsmParser
  Core
  Support
  TestingSupport
  )

add_feme_unittest(FeMeTransformsCPUTests
  ResourceCallsTest.cpp
  UnsupportedOpsTest.cpp
  )
target_link_libraries(FeMeTransformsCPUTests
  PRIVATE
  FeMeTransformsCPU
  )
