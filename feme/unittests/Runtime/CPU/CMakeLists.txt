set(LLVM_LINK_COMPONENTS
  BitReader
  Core
  ExecutionEngine
  MCJIT
  Support
  TestingSupport
  ${LLVM_TARGETS_TO_BUILD}
  )

add_feme_unittest(FeMeRuntimeCPUTests
  RuntimeCPUTest.cpp
  )
target_link_libraries(FeMeRuntimeCPUTests
  PRIVATE
  FeMeRuntimeCPU
  )
