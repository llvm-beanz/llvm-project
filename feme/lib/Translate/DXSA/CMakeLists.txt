add_mlir_library(FeMeTranslateDXSA
  DXSAToLLVMIRTranslator.cpp
  TranslateRegistration.cpp

  LINK_COMPONENTS
  Core
  Support

  LINK_LIBS PUBLIC
  FeMeDXSADialect
  MLIRIR
  MLIRTranslateLib
  )
