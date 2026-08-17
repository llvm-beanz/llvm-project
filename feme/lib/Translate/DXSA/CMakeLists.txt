add_feme_library(FeMeTranslateDXSA
  DXSAToLLVMIRTranslator.cpp
  DXSATranslator.cpp
  TranslateRegistration.cpp

  LINK_COMPONENTS
  Analysis
  BinaryFormat
  Core
  Object
  Support
  TransformUtils

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeDXSADialect
  MLIRIR
  MLIRTranslateLib
  )
