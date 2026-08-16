add_mlir_library(FeMeTransformsGraphics
  CanonicalizeStage.cpp

  LINK_COMPONENTS
  Core
  Support
  TargetParser

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeTransformsDXIL
  )
