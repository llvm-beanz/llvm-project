set(LLVM_LINK_COMPONENTS
  Support
  )

add_feme_unittest(FeMeCoreTests
  ContextTest.cpp
  ModuleTest.cpp
  )
target_link_libraries(FeMeCoreTests
  PRIVATE
  FeMeCore
  MLIRIR
  )
