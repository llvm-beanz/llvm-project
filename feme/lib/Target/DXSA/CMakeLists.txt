add_mlir_translation_library(FeMeTargetDXSA
  BinaryParser.cpp
  BinaryWriter.cpp
  TranslateRegistration.cpp

  ADDITIONAL_HEADER_DIRS
  ${FEME_SOURCE_DIR}/include/feme/Target/DXSA

  LINK_LIBS PUBLIC
  FeMeDXBCAssembler
  FeMeDXSADialect
  MLIRIR
  MLIRSupport
  MLIRTranslateLib
  )
