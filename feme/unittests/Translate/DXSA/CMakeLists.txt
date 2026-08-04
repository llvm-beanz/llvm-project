set(LLVM_LINK_COMPONENTS
  Core
  Support
  )

add_feme_unittest(FeMeTranslateDXSATests
  DXSAToLLVMIRTranslatorTest.cpp
  )
target_link_libraries(FeMeTranslateDXSATests
  PRIVATE
  FeMeDXSADialect
  FeMeTranslateDXSA
  MLIRIR
  MLIRParser
  )
