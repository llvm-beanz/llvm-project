add_mlir_library(FeMeTransformsCPU
  BuiltinCalls.cpp
  CFGGen.cpp
  EntryWrapper.cpp
  Linearize.cpp
  Prepare.cpp
  ResourceCalls.cpp
  ResourceLowering.cpp
  SIMDize.cpp
  UnsupportedOps.cpp
  VerifyStructured.cpp
  WaveLowering.cpp

  LINK_COMPONENTS
  Analysis
  Core
  Passes
  Scalar
  Support
  TransformUtils

  LINK_LIBS PUBLIC
  FeMeAnalysisCPU
  )
