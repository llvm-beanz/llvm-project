add_feme_library(FeMeTransformsDXIL
  IntrinsicExpansion.cpp
  MetadataRaising.cpp
  OpRaising.cpp
  ResourceMetadata.cpp
  RootBindingPlan.cpp
  SignatureImport.cpp
  SPIRVRaising.cpp

  LINK_COMPONENTS
  Core
  Object
  Support
  TargetParser

  LINK_LIBS PUBLIC
  FeMeCore
  )
