set(DXIL_TOOLS_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(DXIL_TOOLS_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

include_directories(
  ${DXIL_TOOLS_SOURCE_DIR}/include
  ${DXIL_TOOLS_BINARY_DIR}/include
  )

# libLLVM also contains modern IR definitions with the same symbol names.
set(LLVM_LINK_LLVM_DYLIB OFF)

add_subdirectory(utils/dxil-tblgen)
add_subdirectory(include/dxil/IR)
add_subdirectory(lib/IR)
add_subdirectory(lib/AsmParser)
add_subdirectory(lib/Bitcode)
add_subdirectory(tools/dxil-as)
add_subdirectory(tools/dxil-dis)

if(LLVM_INCLUDE_TESTS)
  add_subdirectory(test)
endif()
