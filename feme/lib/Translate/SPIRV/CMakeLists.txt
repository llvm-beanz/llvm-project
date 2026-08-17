add_feme_library(FeMeTranslateSPIRV
  SPIRVToLLVMDialectTranslator.cpp
  SPIRVToLLVMTranslator.cpp
  TranslateRegistration.cpp

  LINK_COMPONENTS
  Support

  LINK_LIBS PUBLIC
  FeMeConversionSPIRVToLLVM
  FeMeCore
  FeMeTranslateLLVMIR
  MLIRIR
  MLIRLLVMDialect
  MLIRSPIRVDialect
  MLIRTargetLLVMIRExport
  MLIRBuiltinToLLVMIRTranslation
  MLIRLLVMToLLVMIRTranslation
  MLIRPass
  MLIRTranslateLib
  )
