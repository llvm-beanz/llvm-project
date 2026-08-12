add_mlir_library(FeMeTransformsCPU
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
  Support
  )
