add_mlir_library(FeMeCore
  Context.cpp
  FormatRegistry.cpp
  Module.cpp

  LINK_COMPONENTS
  Core

  LINK_LIBS PUBLIC
  MLIRIR
  )
