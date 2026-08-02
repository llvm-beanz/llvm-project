add_mlir_library(FeMeCore
  Context.cpp

  LINK_COMPONENTS
  Core

  LINK_LIBS PUBLIC
  MLIRIR
  )
