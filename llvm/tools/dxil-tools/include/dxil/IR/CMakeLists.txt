set(LLVM_TARGET_DEFINITIONS Intrinsics.td)

# The legacy intrinsic emitter still needs porting to modern llvm-tblgen.
tablegen(LLVM Intrinsics.gen -gen-intrinsic)

add_public_tablegen_target(dxil_intrinsics_gen)
