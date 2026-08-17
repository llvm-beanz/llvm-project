add_feme_library(FeMeTranslateLLVMIR
  LLVMDialectToLLVMIRTranslator.cpp
  TranslateRegistration.cpp

  LINK_COMPONENTS
  Support

  LINK_LIBS PUBLIC
  FeMeCore
  MLIRIR
  MLIRLLVMDialect
  MLIRTargetLLVMIRExport
  MLIRBuiltinToLLVMIRTranslation
  MLIRLLVMToLLVMIRTranslation
  MLIRTranslateLib
  )
