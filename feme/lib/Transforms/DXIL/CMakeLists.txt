add_mlir_library(FeMeTransformsDXIL
  IntrinsicExpansion.cpp
  MetadataRaising.cpp
  OpRaising.cpp
  ResourceMetadata.cpp
  SPIRVRaising.cpp

  LINK_COMPONENTS
  Core
  Support
  TargetParser

  LINK_LIBS PUBLIC
  FeMeCore
  )
