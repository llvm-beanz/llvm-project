add_subdirectory(CPU)
add_subdirectory(DXSA)

add_feme_library(FeMeTarget
  TargetMachineBackend.cpp
  TranslateRegistration.cpp

  LINK_COMPONENTS
  AllTargetsAsmParsers
  AllTargetsCodeGens
  AllTargetsDescs
  AllTargetsInfos
  Core
  CodeGen
  IRReader
  MC
  Support
  Target
  TargetParser

  LINK_LIBS PUBLIC
  MLIRTranslateLib
  )
