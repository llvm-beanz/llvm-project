add_mlir_library(FeMeTransformsGraphics
  CanonicalizeStage.cpp
  ValidateStage.cpp

  LINK_COMPONENTS
  Core
  Support
  TargetParser

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeTransformsDXIL
  )
