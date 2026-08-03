add_mlir_library(FeMeTransformsDXIL
  MetadataRaising.cpp
  OpRaising.cpp

  LINK_COMPONENTS
  Core
  Support
  TargetParser
  )
