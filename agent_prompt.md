---
model: claude-sonnet-5
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file.

When you build and test ensure that you are using object file caching, and
building with assertions enabled. Also build and test the `check-feme` target
ensuring that all the target dependencies are correctly setup so that the test
dependencies will build before running the tests.

When you deviate from the design document please update the design document.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Building the current state of the feme project on macos fails with the error:

```
[3/31] Linking CXX shared library lib/libfeme_vulkan.dylib
FAILED: lib/libfeme_vulkan.dylib
: && /usr/bin/c++ -fPIC -fvisibility-inlines-hidden -Werror=date-time -Werror=unguarded-availability-new -Wall -Wextra -Wno-unused-parameter -Wwrite-strings -Wcast-qual -Wmissing-field-initializers -pedantic -Wno-long-long -Wc++98-compat-extra-semi -Wimplicit-fallthrough -Wcovered-switch-default -Wno-noexcept-type -Wnon-virtual-dtor -Wdelete-non-virtual-dtor -Wsuggest-override -Wstring-conversion -Wno-pass-failed -Wmisleading-indentation -Wctad-maybe-unsupported -fdiagnostics-color -O2 -g -DNDEBUG -arch arm64 -dynamiclib -Wl,-headerpad_max_install_names -Wl,-dead_strip -Wl,-no_warn_duplicate_libraries  -Wl,--version-script=/Users/cbieneman/dev/llvm-project/feme/tools/feme-vulkan/libfeme_vulkan.map -o lib/libfeme_vulkan.dylib -install_name @rpath/libfeme_vulkan.dylib tools/feme/tools/feme-vulkan/CMakeFiles/feme_vulkan.dir/VulkanICD.cpp.o  -Wl,-rpath,@loader_path/../lib  lib/libFeMeVulkanCore.a  lib/libFeMeTargetCPU.a  lib/libFeMeOptimizer.a  lib/libFeMeRuntimeCPU.a  lib/libFeMeTransformsCPU.a  lib/libFeMeAnalysisCPU.a  lib/libFeMeTransformsGraphics.a  lib/libFeMeTransformsDXIL.a  lib/libFeMeCore.a  lib/libMLIRIR.a  lib/libMLIRSupport.a  lib/libLLVMOrcJIT.a  lib/libLLVMExecutionEngine.a  lib/libLLVMRuntimeDyld.a  lib/libLLVMJITLink.a  lib/libLLVMOrcTargetProcess.a  lib/libLLVMOrcShared.a  lib/libLLVMWindowsDriver.a  lib/libLLVMOption.a  lib/libLLVMAArch64CodeGen.a  lib/libLLVMPasses.a  lib/libLLVMCoroutines.a  lib/libLLVMHipStdPar.a  lib/libLLVMipo.a  lib/libLLVMLinker.a  lib/libLLVMFrontendOpenMP.a  lib/libLLVMFrontendOffloading.a  lib/libLLVMObjectYAML.a  lib/libLLVMFrontendAtomic.a  lib/libLLVMFrontendDirective.a  lib/libLLVMIRPrinter.a  lib/libLLVMInstrumentation.a  lib/libLLVMCFGuard.a  lib/libLLVMGlobalISel.a  lib/libLLVMVectorize.a  lib/libLLVMSandboxIR.a  lib/libLLVMAsmPrinter.a  lib/libLLVMSelectionDAG.a  lib/libLLVMCodeGen.a  lib/libLLVMTarget.a  lib/libLLVMScalarOpts.a  lib/libLLVMAggressiveInstCombine.a  lib/libLLVMInstCombine.a  lib/libLLVMObjCARCOpts.a  lib/libLLVMTransformUtils.a  lib/libLLVMCGData.a  lib/libLLVMBitWriter.a  lib/libLLVMAnalysis.a  lib/libLLVMFrontendHLSL.a  lib/libLLVMProfileData.a  lib/libLLVMSymbolize.a  lib/libLLVMDebugInfoGSYM.a  lib/libLLVMDebugInfoPDB.a  lib/libLLVMDebugInfoCodeView.a  lib/libLLVMDebugInfoMSF.a  lib/libLLVMDebugInfoBTF.a  lib/libLLVMDebugInfoDWARF.a  lib/libLLVMObject.a  lib/libLLVMTextAPI.a  lib/libLLVMIRReader.a  lib/libLLVMBitReader.a  lib/libLLVMAsmParser.a  lib/libLLVMCore.a  lib/libLLVMRemarks.a  lib/libLLVMBitstreamReader.a  lib/libLLVMAArch64AsmParser.a  lib/libLLVMMCParser.a  lib/libLLVMAArch64Disassembler.a  lib/libLLVMAArch64Desc.a  lib/libLLVMCodeGenTypes.a  lib/libLLVMMCDisassembler.a  lib/libLLVMAArch64Info.a  lib/libLLVMMC.a  lib/libLLVMDebugInfoDWARFLowLevel.a  lib/libLLVMBinaryFormat.a  lib/libLLVMAArch64Utils.a  lib/libLLVMTargetParser.a  lib/libLLVMSupport.a  -lm  /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/lib/libz.tbd  lib/libLLVMDemangle.a && :
ld: unknown options: --version-script=/Users/cbieneman/dev/llvm-project/feme/tools/feme-vulkan/libfeme_vulkan.map
```

Can you please fix this?
