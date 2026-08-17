add_mlir_library(FeMeGraphics
  Executor.cpp
  ImageFixture.cpp
  Pipeline.cpp
  Scene.cpp

  LINK_COMPONENTS
  Core
  Support

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeTargetCPU
  )
