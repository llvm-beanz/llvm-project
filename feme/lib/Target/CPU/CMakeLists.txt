add_mlir_library(FeMeTargetCPU
  CompiledStage.cpp
  JITEngine.cpp
  Pipeline.cpp
  ResourceHeap.cpp
  ResourceInfo.cpp
  WaveSize.cpp

  LINK_COMPONENTS
  BitReader
  BitWriter
  Core
  ExecutionEngine
  Linker
  OrcJIT
  Passes
  Support
  native

  LINK_LIBS PUBLIC
  FeMeAnalysisCPU
  FeMeCore
  FeMeOptimizer
  FeMeRuntimeCPU
  FeMeTransformsCPU
  FeMeTransformsGraphics
  )
