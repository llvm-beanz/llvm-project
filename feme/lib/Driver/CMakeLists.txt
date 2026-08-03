add_mlir_library(FeMeDriver
  Driver.cpp

  LINK_COMPONENTS
  Core
  Support
  TargetParser

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeFrontend
  FeMeImportDXIL
  FeMeImportSPIRV
  FeMeTarget
  FeMeTransformsAMDGPU
  FeMeTransformsDXIL
  FeMeTransformsSPIRV
  FeMeTranslateSPIRV
  )
