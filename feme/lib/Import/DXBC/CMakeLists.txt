add_feme_library(FeMeImportDXBC
  DXBCImporter.cpp
  TranslateRegistration.cpp

  LINK_COMPONENTS
  BinaryFormat
  Core
  Object
  Support

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeDXSADialect
  FeMeTargetDXSA
  MLIRIR
  MLIRTranslateLib
  )
