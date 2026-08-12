set(LLVM_LINK_COMPONENTS
  AsmParser
  Core
  Support
  TestingSupport
  )

add_feme_unittest(FeMeTransformsCPUTests
  PrepareTest.cpp
  ResourceCallsTest.cpp
  ResourceLoweringTest.cpp
  UnsupportedOpsTest.cpp
  )
target_link_libraries(FeMeTransformsCPUTests
  PRIVATE
  FeMeTransformsCPU
  )
