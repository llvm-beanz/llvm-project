add_llvm_library(DXILBitReader
  BitcodeReader.cpp

  ADDITIONAL_HEADER_DIRS
  ${DXIL_TOOLS_SOURCE_DIR}/include/dxil/Bitcode

  DEPENDS
  dxil_intrinsics_gen

  LINK_LIBS
  DXILCore

  LINK_COMPONENTS
  BitstreamReader
  Support
  )
