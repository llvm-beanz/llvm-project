add_mlir_library(FeMeDriver
  Driver.cpp

  LINK_COMPONENTS
  Core
  CodeGen
  Support
  Target
  TargetParser

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeFrontend
  FeMeImportDXIL
  FeMeImportSPIRV
  FeMeOptimizer
  FeMeTarget
  FeMeTargetCPU
  FeMeTransformsAMDGPU
  FeMeTransformsDXIL
  FeMeTransformsSPIRV
  FeMeTranslateSPIRV
  )
