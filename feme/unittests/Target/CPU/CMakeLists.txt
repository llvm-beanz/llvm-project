set(LLVM_LINK_COMPONENTS
  AsmParser
  Core
  Support
  TestingSupport
  )

add_feme_unittest(FeMeTargetCPUTests
  JITEngineTest.cpp
  ResourceHeapTest.cpp
  ResourceInfoTest.cpp
  WaveSizeTest.cpp
  )
target_link_libraries(FeMeTargetCPUTests
  PRIVATE
  FeMeCore
  FeMeTargetCPU
  )
