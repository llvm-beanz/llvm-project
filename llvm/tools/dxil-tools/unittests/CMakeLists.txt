set(LLVM_LINK_COMPONENTS Support)

add_custom_target(DXILUnitTests)
set_target_properties(DXILUnitTests PROPERTIES FOLDER "LLVM/Tests")

add_unittest(DXILUnitTests DXILTests
  BitcodeHeaderTest.cpp
  IRHeadersTest.cpp

  DEPENDS
  intrinsics_gen
  )

add_custom_target(check-dxil-tools-unit
  COMMAND $<TARGET_FILE:DXILTests>
  DEPENDS DXILTests
  USES_TERMINAL
  )
add_dependencies(check-dxil-tools check-dxil-tools-unit)
