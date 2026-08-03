set(LLVM_LINK_COMPONENTS
  BinaryFormat
  Support
  )

add_llvm_tool(dxbc-as
  dxbc-as.cpp
  )

target_link_libraries(dxbc-as PRIVATE FeMeDXBCAssembler)
