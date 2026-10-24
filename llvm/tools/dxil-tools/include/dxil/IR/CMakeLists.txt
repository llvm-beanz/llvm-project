set(LLVM_TARGET_DEFINITIONS Intrinsics.td)

tablegen(DXIL Intrinsics.gen -gen-intrinsic)

add_public_tablegen_target(dxil_intrinsics_gen)
