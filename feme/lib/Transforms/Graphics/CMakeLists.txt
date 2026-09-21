add_feme_library(FeMeTransformsGraphics
  CanonicalizeStage.cpp
  UnrollConstantTripCountLoops.cpp
  ValidateStage.cpp

  LINK_COMPONENTS
  Core
  Passes
  Support
  TargetParser
  TransformUtils

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeTransformsDXIL
  )
