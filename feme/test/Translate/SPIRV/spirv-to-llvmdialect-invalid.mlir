// RUN: not feme-translate --spirv-to-llvmdialect %s 2>&1 | FileCheck %s

// feme::SPIRVToLLVMDialectTranslator (via feme-translate's
// `--spirv-to-llvmdialect`) requires a `spirv.module` top-level operation;
// check that a well-formed but non-SPIR-V module is rejected with a clear
// diagnostic rather than silently mistranslated.

module {}

// CHECK: expected a 'spirv.module' op, got 'builtin.module'
