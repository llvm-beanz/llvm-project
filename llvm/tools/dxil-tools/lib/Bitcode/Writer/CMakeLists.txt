add_llvm_library(DXILBitWriter
  BitWriter.cpp
  BitcodeWriter.cpp
  BitcodeWriterPass.cpp
  ValueEnumerator.cpp

  DEPENDS
  dxil_intrinsics_gen

  LINK_LIBS
  DXILCore

  LINK_COMPONENTS
  Support
  )
