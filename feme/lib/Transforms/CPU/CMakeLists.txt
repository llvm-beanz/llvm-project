add_mlir_library(FeMeTransformsCPU
  EntryWrapper.cpp
  Linearize.cpp
  Prepare.cpp
  ResourceLowering.cpp
  SIMDize.cpp
  WaveLowering.cpp

  LINK_COMPONENTS
  Core
  Support
  )
