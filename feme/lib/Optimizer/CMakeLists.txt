add_llvm_library(FeMeOptimizer
  OptimizerPipeline.cpp

  LINK_COMPONENTS
  Core
  Passes
  Support
  Target
  )
