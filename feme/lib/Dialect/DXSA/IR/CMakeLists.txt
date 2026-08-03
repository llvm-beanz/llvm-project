add_mlir_dialect_library(FeMeDXSADialect
  DXSA.cpp
  DXSAOperand.cpp

  ADDITIONAL_HEADER_DIRS
  ${FEME_SOURCE_DIR}/include/feme/Dialect/DXSA

  DEPENDS
  MLIRDXSAOpsIncGen

  LINK_LIBS PUBLIC
  MLIRDialect
  MLIRIR
  )
