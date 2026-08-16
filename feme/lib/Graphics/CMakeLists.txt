add_mlir_library(FeMeGraphics
  Pipeline.cpp

  LINK_COMPONENTS
  Core
  Support

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeTargetCPU
  )
