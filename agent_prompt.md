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

The tests are currently failing. Please fix the issues:

```
cbieneman@MacBook-Pro-3 ~/d/l/build-dbg (cbieneman/feme)> bin/feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S /Users/cbieneman/dev/llvm-project/feme/test/Transforms/CPU/simdize-math-libcall.ll
/Users/cbieneman/dev/llvm-project/llvm/lib/IR/Intrinsics.cpp:806:33: runtime error: member call on null pointer of type 'llvm::Module'
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior /Users/cbieneman/dev/llvm-project/llvm/lib/IR/Intrinsics.cpp:806:33
PLEASE submit a bug report to https://github.com/llvm/llvm-project/issues/ and include the crash backtrace and instructions to reproduce the bug.
Stack dump:
0.      Program arguments: bin/feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S /Users/cbieneman/dev/llvm-project/feme/test/Transforms/CPU/simdize-math-libcall.ll
1.      Running pass "feme-cpu-simdize" on module "/Users/cbieneman/dev/llvm-project/feme/test/Transforms/CPU/simdize-math-libcall.ll"
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  feme-opt                            0x0000000104c0e9f8 llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  feme-opt                            0x0000000104c0fd70 PrintStackTraceSignalHandler(void*) + 112
2  feme-opt                            0x0000000104c09378 llvm::sys::RunSignalHandlers() + 524
3  feme-opt                            0x0000000104c13dc0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib            0x000000018536f744 _sigtramp + 56
5  libsystem_pthread.dylib             0x00000001853658d8 pthread_kill + 296
6  libsystem_c.dylib                   0x000000018526c644 abort + 148
7  libclang_rt.ubsan_osx_dynamic.dylib 0x000000017029c298 __sanitizer::Atexit(void (*)()) + 0
8  libclang_rt.ubsan_osx_dynamic.dylib 0x000000017029b890 __sanitizer::Die() + 108
9  libclang_rt.ubsan_osx_dynamic.dylib 0x000000017027f6c0 __ubsan_handle_alignment_assumption + 0
10 feme-opt                            0x0000000103ae2ecc llvm::Intrinsic::getOrInsertDeclaration(llvm::Module*, unsigned int, llvm::ArrayRef<llvm::Type*>) + 80
11 feme-opt                            0x0000000105c8a358 (anonymous namespace)::FunctionWidener::widenElementwise(llvm::Instruction&, llvm::IRBuilder<llvm::ConstantFolder, llvm::IRBuilderDefaultInserter>&) + 940
12 feme-opt                            0x0000000105c7fad0 (anonymous namespace)::FunctionWidener::widenInstruction(llvm::Instruction&, llvm::IRBuilder<llvm::ConstantFolder, llvm::IRBuilderDefaultInserter>&) + 2656
13 feme-opt                            0x0000000105c78cac (anonymous namespace)::FunctionWidener::widen() + 2232
14 feme-opt                            0x0000000105c78134 feme::cpu::SIMDizePass::run(llvm::Module&, llvm::AnalysisManager<llvm::Module>&) + 1252
15 feme-opt                            0x0000000102eaca44 llvm::detail::PassModel<llvm::Module, feme::cpu::SIMDizePass, llvm::AnalysisManager<llvm::Module>>::runImpl(llvm::detail::PassConcept<llvm::Module, llvm::AnalysisManager<llvm::Module>>&, llvm::Module&, llvm::AnalysisManager<llvm::Module>&) + 264
16 feme-opt                            0x0000000103e52248 llvm::detail::PassConcept<llvm::Module, llvm::AnalysisManager<llvm::Module>>::run(llvm::Module&, llvm::AnalysisManager<llvm::Module>&) + 284
17 feme-opt                            0x0000000103e5139c llvm::PassManager<llvm::Module, llvm::AnalysisManager<llvm::Module>>::run(llvm::Module&, llvm::AnalysisManager<llvm::Module>&) + 1032
18 feme-opt                            0x0000000102e8b230 (anonymous namespace)::runLLVMIRMode(int, char**) + 1864
19 feme-opt                            0x0000000102e8a7dc main + 544
20 dyld                                0x0000000184fa7e00 start + 6992
fish: Job 1, 'bin/feme-opt --llvm -passes=fem…' terminated by signal SIGABRT (Abort)
```
