set(LLVM_LINK_COMPONENTS
  MC
  Support
  )

add_llvm_tool(dxil-as
  DXContainer.cpp
  dxil-as.cpp

  DEPENDS
  dxil_intrinsics_gen
  )

target_link_libraries(dxil-as PRIVATE DXILAsmParser DXILBitWriter DXILCore)
