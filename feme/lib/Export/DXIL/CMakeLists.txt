add_feme_library(FeMeExportDXIL
  DXILExporter.cpp

  LINK_COMPONENTS
  Core
  Support
  TargetParser

  LINK_LIBS PUBLIC
  FeMeCore
  FeMeTarget
  )
