set(LLVM_LINK_COMPONENTS Support)

include_directories(${LLVM_MAIN_SRC_DIR}/utils/TableGen/Basic)

add_tablegen(dxil-tblgen DXIL
  CodeGenIntrinsics.cpp
  IntrinsicEmitter.cpp
  dxil-tblgen.cpp
  )
