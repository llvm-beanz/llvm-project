add_feme_library(FeMeConversionSPIRVToLLVM
  ConvertSPIRVToLLVMPass.cpp
  SPIRVToLLVMPatterns.cpp
  StageIODecorations.cpp

  LINK_COMPONENTS
  Core
  Support
  TargetParser

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeGraphics
  MLIRIR
  MLIRLLVMDialect
  MLIRPass
  MLIRSPIRVDialect
  MLIRSPIRVToLLVM
  MLIRTransforms
  )
