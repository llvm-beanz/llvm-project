add_mlir_library(FeMeTransformsDXIL
  MetadataRaising.cpp
  OpRaising.cpp
  ResourceMetadata.cpp

  LINK_COMPONENTS
  Core
  Support
  TargetParser
  )
