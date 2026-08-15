set(LLVM_LINK_COMPONENTS
  Core
  Support
  TargetParser
  )

add_feme_unittest(FeMeCoreTests
  ContextTest.cpp
  FormatRegistryTest.cpp
  ModuleTest.cpp
  ShaderStageTest.cpp
  )
target_link_libraries(FeMeCoreTests
  PRIVATE
  FeMeCore
  MLIRIR
  )
