set(LLVM_LINK_COMPONENTS Support)

add_custom_target(DXILUnitTests)
set_target_properties(DXILUnitTests PROPERTIES FOLDER "LLVM/Tests")

add_unittest(DXILUnitTests DXILTests
  AsmParserTest.cpp
  AttributeImplTest.cpp
  BitcodeHeaderTest.cpp
  ConstantsContextTest.cpp
  IRHeadersTest.cpp
  IRTest.cpp
  IntrinsicsTest.cpp
  PatternMatchTest.cpp
  UseTest.cpp

  DEPENDS
  dxil_intrinsics_gen
  intrinsics_gen
  )

target_link_libraries(DXILTests PRIVATE DXILAsmParser DXILCore)

add_custom_target(check-dxil-tools-unit
  COMMAND $<TARGET_FILE:DXILTests>
  DEPENDS DXILTests
  USES_TERMINAL
  )
add_dependencies(check-dxil-tools check-dxil-tools-unit)
