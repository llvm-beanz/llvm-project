add_llvm_library(FeMeFrontend
  Options.cpp

  DEPENDS
  FeMeFrontendOptionsTableGen

  LINK_COMPONENTS
  Option
  Support
  )
