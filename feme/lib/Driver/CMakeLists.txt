add_mlir_library(FeMeDriver
  Driver.cpp

  LINK_COMPONENTS
  Core
  CodeGen
  Object
  Support
  Target
  TargetParser

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeExportDXIL
  FeMeExportSPIRV
  FeMeFrontend
  FeMeImportDXBC
  FeMeImportDXIL
  FeMeImportSPIRV
  FeMeOptimizer
  FeMeTarget
  FeMeTargetCPU
  FeMeTransformsAMDGPU
  FeMeTransformsNVPTX
  FeMeTransformsCPU
  FeMeTransformsDXIL
  FeMeTransformsSPIRV
  FeMeTranslateDXSA
  FeMeTranslateSPIRV
  )
