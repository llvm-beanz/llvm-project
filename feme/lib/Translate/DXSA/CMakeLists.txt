add_mlir_library(FeMeTranslateDXSA
  DXSAToLLVMIRTranslator.cpp
  TranslateRegistration.cpp

  LINK_COMPONENTS
  BinaryFormat
  Core
  Object
  Support

  LINK_LIBS PUBLIC
  FeMeDXSADialect
  MLIRIR
  MLIRTranslateLib
  )
