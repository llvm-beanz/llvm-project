add_feme_library(FeMeGraphics
  DomainInvocations.cpp
  Executor.cpp
  Geometry.cpp
  GeometryInputs.cpp
  GeometryStream.cpp
  GeometryStreamCollection.cpp
  ImageFixture.cpp
  LayeredRendering.cpp
  Mesh.cpp
  MeshOutput.cpp
  Patch.cpp
  PatchPipeline.cpp
  Pipeline.cpp
  Scene.cpp
  StageLink.cpp
  StageStorage.cpp
  TaskPayload.cpp
  Tessellation.cpp
  Tessellator.cpp

  LINK_COMPONENTS
  Core
  Support

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeTargetCPU
  )
