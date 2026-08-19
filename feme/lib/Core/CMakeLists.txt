add_feme_library(FeMeCore
  Context.cpp
  FormatRegistry.cpp
  Module.cpp
  RaisedIRVerifier.cpp
  ShaderStage.cpp
  Signature.cpp
  StageOps.cpp

  LINK_COMPONENTS
  Core
  Support
  TargetParser

  LINK_LIBS PUBLIC
  MLIRIR
  )
