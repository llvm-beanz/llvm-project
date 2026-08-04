add_mlir_library(FeMeTranslateDXSA
  DXSAToLLVMIRTranslator.cpp
  TranslateRegistration.cpp

  LINK_COMPONENTS
  Analysis
  BinaryFormat
  Core
  Object
  Support
  TransformUtils

  LINK_LIBS PUBLIC
  FeMeDXSADialect
  MLIRIR
  MLIRTranslateLib
  )
