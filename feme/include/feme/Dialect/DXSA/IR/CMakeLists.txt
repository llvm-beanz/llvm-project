set(LLVM_TARGET_DEFINITIONS DXSAOps.td)
mlir_tablegen(DXSAOpsEnums.h.inc -gen-enum-decls)
mlir_tablegen(DXSAOpsEnums.cpp.inc -gen-enum-defs)
mlir_tablegen(DXSAOpsAttributes.h.inc -gen-attrdef-decls
              -attrdefs-dialect=dxsa)
mlir_tablegen(DXSAOpsAttributes.cpp.inc -gen-attrdef-defs
              -attrdefs-dialect=dxsa)
add_mlir_dialect(DXSAOps dxsa)
add_mlir_doc(DXSAOps DXSADialect Dialects/ -gen-dialect-doc)
