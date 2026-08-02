set(LLVM_LINK_COMPONENTS
  Option
  Support
  )

add_feme_unittest(FeMeFrontendTests
  OptionsTest.cpp
  FrontendOptionsTest.cpp
  )
target_link_libraries(FeMeFrontendTests
  PRIVATE
  FeMeFrontend
  )
