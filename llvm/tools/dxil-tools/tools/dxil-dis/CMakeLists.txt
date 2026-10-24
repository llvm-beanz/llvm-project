set(LLVM_LINK_COMPONENTS
  Support
  )

add_llvm_tool(dxil-dis
  dxil-dis.cpp

  DEPENDS
  dxil_intrinsics_gen
  )

target_link_libraries(dxil-dis PRIVATE DXILBitReader DXILCore)
