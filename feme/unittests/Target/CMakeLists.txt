# TargetMachineBackendTest specifically exercises the SPIR-V retargeting
# "null pipeline" (see feme/docs/Design.md), which requires LLVM's SPIRV
# target to be part of this build; skip it (rather than failing the whole
# build) when SPIRV isn't in LLVM_TARGETS_TO_BUILD.
if(LLVM_TARGETS_TO_BUILD MATCHES "SPIRV")
  set(LLVM_LINK_COMPONENTS
    Support
    TestingSupport
    SPIRV
    )

  add_feme_unittest(FeMeTargetTests
    TargetMachineBackendTest.cpp
    )
  target_link_libraries(FeMeTargetTests
    PRIVATE
    FeMeCore
    FeMeImportSPIRV
    FeMeTarget
    FeMeTranslateSPIRV
    MLIRIR
    MLIRParser
    MLIRSPIRVDialect
    MLIRSPIRVSerialization
    )
endif()
