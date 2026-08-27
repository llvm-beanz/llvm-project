add_feme_library(FeMeGraphics
  DomainInvocations.cpp
  Executor.cpp
  GeometryInputs.cpp
  GeometryStream.cpp
  GeometryStreamCollection.cpp
  ImageFixture.cpp
  LayeredRendering.cpp
  Patch.cpp
  PatchPipeline.cpp
  Pipeline.cpp
  Scene.cpp
  StageStorage.cpp
  Tessellator.cpp

  LINK_COMPONENTS
  Core
  Support

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeTargetCPU
  )
