set(LLVM_LINK_COMPONENTS
  AsmParser
  Core
  Support
  TestingSupport
  )

add_feme_unittest(FeMeTargetCPUTests
  ResourceInfoTest.cpp
  WaveSizeTest.cpp
  )
target_link_libraries(FeMeTargetCPUTests
  PRIVATE
  FeMeTargetCPU
  )
