add_mlir_library(FeMeImportDXIL
  DXILImporter.cpp
  TranslateRegistration.cpp

  LINK_COMPONENTS
  BitReader
  Core
  Object
  Support

  LINK_LIBS PUBLIC
  FeMeCore
  MLIRTranslateLib
  )
