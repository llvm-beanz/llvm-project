set(LLVM_LINK_COMPONENTS
  AsmParser
  Core
  Support
  )

add_feme_unittest(FeMeOptimizerTests
  OptimizerPipelineTest.cpp
  )
target_link_libraries(FeMeOptimizerTests
  PRIVATE
  FeMeOptimizer
  )
