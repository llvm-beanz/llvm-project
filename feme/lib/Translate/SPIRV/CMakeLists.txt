add_mlir_library(FeMeTranslateSPIRV
  SPIRVToLLVMTranslator.cpp
  TranslateRegistration.cpp

  LINK_COMPONENTS
  Support

  LINK_LIBS PUBLIC
  FeMeCore
  MLIRIR
  MLIRLLVMDialect
  MLIRSPIRVDialect
  MLIRSPIRVToLLVM
  MLIRTargetLLVMIRExport
  MLIRBuiltinToLLVMIRTranslation
  MLIRLLVMToLLVMIRTranslation
  MLIRPass
  MLIRTranslateLib
  )
