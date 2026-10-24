set(LLVM_OPTIONAL_SOURCES BitReader.cpp)

add_llvm_library(DXILBitReader
  # BitReader.cpp # HLSL Change - this is just the C wrapper
  BitcodeReader.cpp
  BitstreamReader.cpp

  ADDITIONAL_HEADER_DIRS
  ${DXIL_TOOLS_SOURCE_DIR}/include/dxil/Bitcode

  DEPENDS
  dxil_intrinsics_gen

  LINK_LIBS
  DXILCore

  LINK_COMPONENTS
  Support
  )
