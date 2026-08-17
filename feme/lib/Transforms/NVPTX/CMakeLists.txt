add_feme_library(FeMeTransformsNVPTX
  RaisedLowering.cpp
  ResourceLowering.cpp

  LINK_COMPONENTS
  Core
  Support
  TargetParser

  LINK_LIBS PUBLIC
  FeMeCore
  )
