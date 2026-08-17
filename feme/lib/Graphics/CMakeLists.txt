add_mlir_library(FeMeGraphics
  Executor.cpp
  GeometryStream.cpp
  ImageFixture.cpp
  Patch.cpp
  Pipeline.cpp
  Scene.cpp
  Tessellator.cpp

  LINK_COMPONENTS
  Core
  Support

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeTargetCPU
  )
