add_llvm_library(FeMeTarget
  TargetMachineBackend.cpp

  LINK_COMPONENTS
  Core
  CodeGen
  MC
  Support
  Target
  TargetParser
  )
