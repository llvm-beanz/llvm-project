add_mlir_library(FeMeTransformsCPU
  BuiltinCalls.cpp
  EntryWrapper.cpp
  Linearize.cpp
  Prepare.cpp
  ResourceCalls.cpp
  ResourceLowering.cpp
  SIMDize.cpp
  UnsupportedOps.cpp
  WaveLowering.cpp

  LINK_COMPONENTS
  Core
  Passes
  Scalar
  Support
  TransformUtils
  )
