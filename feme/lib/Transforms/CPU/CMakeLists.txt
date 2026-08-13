add_mlir_library(FeMeTransformsCPU
  BarrierCalls.cpp
  BoundResourceNormalization.cpp
  BuiltinCalls.cpp
  CFGGen.cpp
  EntryWrapper.cpp
  GroupShared.cpp
  Linearize.cpp
  MaskIntrinsics.cpp
  Prepare.cpp
  ReferenceEntryWrapper.cpp
  ReferenceLowering.cpp
  ResourceCalls.cpp
  ResourceLowering.cpp
  SIMDize.cpp
  UnsupportedOps.cpp
  VerifyStructured.cpp
  WaveCalls.cpp
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
