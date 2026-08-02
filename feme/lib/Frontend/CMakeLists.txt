add_llvm_library(FeMeFrontend
  Options.cpp
  FrontendOptions.cpp

  DEPENDS
  FeMeFrontendOptionsTableGen

  LINK_COMPONENTS
  Option
  Support
  )
