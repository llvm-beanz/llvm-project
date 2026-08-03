add_mlir_library(FeMeTransformsAMDGPU
  RaisedLowering.cpp
  ResourceLowering.cpp

  LINK_COMPONENTS
  Core
  Support
  )
