add_mlir_library(FeMeTranslateSPIRV
  SPIRVToLLVMTranslator.cpp

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
  )
