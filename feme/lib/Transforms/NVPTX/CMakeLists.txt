add_mlir_library(FeMeTransformsNVPTX
  RaisedLowering.cpp
  ResourceLowering.cpp

  LINK_COMPONENTS
  Core
  Support
  )
