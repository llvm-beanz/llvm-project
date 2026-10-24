set(LLVM_LINK_COMPONENTS
  Support
  )

add_llvm_tool(dxil-as
  dxil-as.cpp

  DEPENDS
  dxil_intrinsics_gen
  )

target_link_libraries(dxil-as PRIVATE DXILAsmParser DXILBitWriter DXILCore)
