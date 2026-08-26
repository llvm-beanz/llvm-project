add_feme_library(FeMeTransformsGraphics
  CanonicalizeStage.cpp
  ValidateStage.cpp

  LINK_COMPONENTS
  Core
  Support
  TargetParser
  TransformUtils

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeTransformsDXIL
  )
