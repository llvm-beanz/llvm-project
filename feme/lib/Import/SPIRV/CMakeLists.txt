add_mlir_library(FeMeImportSPIRV
  SPIRVImporter.cpp
  TranslateRegistration.cpp

  LINK_COMPONENTS
  Support

  LINK_LIBS PUBLIC
  FeMeCore
  MLIRIR
  MLIRSPIRVDialect
  MLIRSPIRVDeserialization
  MLIRTranslateLib
  )
