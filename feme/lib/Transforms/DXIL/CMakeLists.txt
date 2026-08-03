add_mlir_library(FeMeTransformsDXIL
  IntrinsicExpansion.cpp
  MetadataRaising.cpp
  OpRaising.cpp
  ResourceMetadata.cpp

  LINK_COMPONENTS
  Core
  Support
  TargetParser
  )
