add_mlir_library(FeMeTransformsAMDGPU
  RaisedLowering.cpp
  ResourceLowering.cpp

  LINK_COMPONENTS
  Core
  Support
  TargetParser

  LINK_LIBS PUBLIC
  FeMeCore
  )
