add_mlir_library(FeMeCore
  Context.cpp
  FormatRegistry.cpp
  Module.cpp
  ShaderStage.cpp

  LINK_COMPONENTS
  Core
  Support
  TargetParser

  LINK_LIBS PUBLIC
  MLIRIR
  )
