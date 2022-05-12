add_llvm_component_group(DirectX)

set(LLVM_TARGET_DEFINITIONS DirectX.td)

tablegen(LLVM DirectXGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM DirectXGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM DirectXGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(DirectXCommonTableGen)

add_llvm_target(DirectXCodeGen
  DirectXAsmPrinter.cpp
  DirectXContainerObjectWriter.cpp
  DirectXInstrInfo.cpp
  DirectXRegisterInfo.cpp
  DirectXSubtarget.cpp
  DirectXTargetMachine.cpp
  DXILOpLowering.cpp
  DXILPointerType.cpp
  DXILPrepare.cpp
  DXILTranslateMetadata.cpp
  PointerTypeAnalysis.cpp

  LINK_COMPONENTS
  Core
  Support
  DirectXInfo
  DXILBitWriter

  ADD_TO_COMPONENT
  DirectX
)

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
add_subdirectory(DXILWriter)
