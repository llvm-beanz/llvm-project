add_mlir_library(FeMeExportSPIRV
  SPIRVExporter.cpp

  LINK_COMPONENTS
  Core
  Support
  TargetParser

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeTarget
  )
