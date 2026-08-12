set(LLVM_LINK_COMPONENTS
  Support
  )

add_feme_unittest(FeMeTargetCPUTests
  WaveSizeTest.cpp
  )
target_link_libraries(FeMeTargetCPUTests
  PRIVATE
  FeMeTargetCPU
  )
