set(LLVM_LINK_COMPONENTS
  Analysis
  AsmParser
  Core
  Support
  )

add_feme_unittest(FeMeAnalysisCPUTests
  WaveUniformityTest.cpp
  )
target_link_libraries(FeMeAnalysisCPUTests
  PRIVATE
  FeMeAnalysisCPU
  )
