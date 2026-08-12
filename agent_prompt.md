---
model: claude-sonnet-5
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file.

When you build and test ensure that you are using object file caching, and
building with assertions enabled.

When you deviate from the design document please update the design document.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Several of FeMe's unit tests are now failing for me. Please fix them:

[17/18] Running the feme regression tests
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/0/11 (565 of 622)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/0/11' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-0-11.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=11 GTEST_SHARD_INDEX=0 /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 1 of 11.
[==========] Running 1 test from 1 test suite.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadIdentityFormat
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x0000000100433ff0 llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 56
1  FeMeRuntimeCPUTests      0x0000000100431e8c llvm::sys::RunSignalHandlers() + 96
2  FeMeRuntimeCPUTests      0x0000000100434af8 SignalHandler(int, __siginfo*, void*) + 328
3  libsystem_platform.dylib 0x000000018536f744 _sigtramp + 56
4  FeMeRuntimeCPUTests      0x0000000100198194 (anonymous namespace)::RuntimeCPUTest::addLoadWrapper(llvm::StringRef, llvm::StringRef) + 76
5  FeMeRuntimeCPUTests      0x0000000100195c50 (anonymous namespace)::RuntimeCPUTest_TypedLoadIdentityFormat_Test::TestBody() + 112
6  FeMeRuntimeCPUTests      0x0000000100751e4c testing::Test::Run() + 228
7  FeMeRuntimeCPUTests      0x000000010075293c testing::TestInfo::Run() + 316
8  FeMeRuntimeCPUTests      0x0000000100753124 testing::TestSuite::Run() + 884
9  FeMeRuntimeCPUTests      0x00000001007610b4 testing::internal::UnitTestImpl::RunAllTests() + 1044
10 FeMeRuntimeCPUTests      0x0000000100760c70 testing::UnitTest::Run() + 120
11 FeMeRuntimeCPUTests      0x0000000100745910 main + 152
12 dyld                     0x0000000184fa7e00 start + 6992

--
exit: -11
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-0-11.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/1/11 (566 of 622)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/1/11' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-1-11.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=11 GTEST_SHARD_INDEX=1 /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 2 of 11.
[==========] Running 1 test from 1 test suite.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadPackedR8G8B8A8Unorm
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x000000010516fff0 llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 56
1  FeMeRuntimeCPUTests      0x000000010516de8c llvm::sys::RunSignalHandlers() + 96
2  FeMeRuntimeCPUTests      0x0000000105170af8 SignalHandler(int, __siginfo*, void*) + 328
3  libsystem_platform.dylib 0x000000018536f744 _sigtramp + 56
4  FeMeRuntimeCPUTests      0x0000000104ed4194 (anonymous namespace)::RuntimeCPUTest::addLoadWrapper(llvm::StringRef, llvm::StringRef) + 76
5  FeMeRuntimeCPUTests      0x0000000104ed4b18 (anonymous namespace)::RuntimeCPUTest_TypedLoadPackedR8G8B8A8Unorm_Test::TestBody() + 108
6  FeMeRuntimeCPUTests      0x000000010548de4c testing::Test::Run() + 228
7  FeMeRuntimeCPUTests      0x000000010548e93c testing::TestInfo::Run() + 316
8  FeMeRuntimeCPUTests      0x000000010548f124 testing::TestSuite::Run() + 884
9  FeMeRuntimeCPUTests      0x000000010549d0b4 testing::internal::UnitTestImpl::RunAllTests() + 1044
10 FeMeRuntimeCPUTests      0x000000010549cc70 testing::UnitTest::Run() + 120
11 FeMeRuntimeCPUTests      0x0000000105481910 main + 152
12 dyld                     0x0000000184fa7e00 start + 6992

--
exit: -11
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-1-11.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/10/11 (567 of 622)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/10/11' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-10-11.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=11 GTEST_SHARD_INDEX=10 /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 11 of 11.
[==========] Running 1 test from 1 test suite.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.StructuredBufferKindIsAccepted
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x00000001003efff0 llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 56
1  FeMeRuntimeCPUTests      0x00000001003ede8c llvm::sys::RunSignalHandlers() + 96
2  FeMeRuntimeCPUTests      0x00000001003f0af8 SignalHandler(int, __siginfo*, void*) + 328
3  libsystem_platform.dylib 0x000000018536f744 _sigtramp + 56
4  FeMeRuntimeCPUTests      0x0000000100154194 (anonymous namespace)::RuntimeCPUTest::addLoadWrapper(llvm::StringRef, llvm::StringRef) + 76
5  FeMeRuntimeCPUTests      0x00000001001575a4 (anonymous namespace)::RuntimeCPUTest_StructuredBufferKindIsAccepted_Test::TestBody() + 100
6  FeMeRuntimeCPUTests      0x000000010070de4c testing::Test::Run() + 228
7  FeMeRuntimeCPUTests      0x000000010070e93c testing::TestInfo::Run() + 316
8  FeMeRuntimeCPUTests      0x000000010070f124 testing::TestSuite::Run() + 884
9  FeMeRuntimeCPUTests      0x000000010071d0b4 testing::internal::UnitTestImpl::RunAllTests() + 1044
10 FeMeRuntimeCPUTests      0x000000010071cc70 testing::UnitTest::Run() + 120
11 FeMeRuntimeCPUTests      0x0000000100701910 main + 152
12 dyld                     0x0000000184fa7e00 start + 6992

--
exit: -11
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-10-11.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/2/11 (568 of 622)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/2/11' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-2-11.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=11 GTEST_SHARD_INDEX=2 /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 3 of 11.
[==========] Running 1 test from 1 test suite.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadOutOfBoundsIndexReadsZeroWithoutTouchingHeap
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x0000000102c0bff0 llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 56
1  FeMeRuntimeCPUTests      0x0000000102c09e8c llvm::sys::RunSignalHandlers() + 96
2  FeMeRuntimeCPUTests      0x0000000102c0caf8 SignalHandler(int, __siginfo*, void*) + 328
3  libsystem_platform.dylib 0x000000018536f744 _sigtramp + 56
4  FeMeRuntimeCPUTests      0x0000000102970194 (anonymous namespace)::RuntimeCPUTest::addLoadWrapper(llvm::StringRef, llvm::StringRef) + 76
5  FeMeRuntimeCPUTests      0x00000001029710a0 (anonymous namespace)::RuntimeCPUTest_TypedLoadOutOfBoundsIndexReadsZeroWithoutTouchingHeap_Test::TestBody() + 64
6  FeMeRuntimeCPUTests      0x0000000102f29e4c testing::Test::Run() + 228
7  FeMeRuntimeCPUTests      0x0000000102f2a93c testing::TestInfo::Run() + 316
8  FeMeRuntimeCPUTests      0x0000000102f2b124 testing::TestSuite::Run() + 884
9  FeMeRuntimeCPUTests      0x0000000102f390b4 testing::internal::UnitTestImpl::RunAllTests() + 1044
10 FeMeRuntimeCPUTests      0x0000000102f38c70 testing::UnitTest::Run() + 120
11 FeMeRuntimeCPUTests      0x0000000102f1d910 main + 152
12 dyld                     0x0000000184fa7e00 start + 6992

--
exit: -11
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-2-11.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/3/11 (569 of 622)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/3/11' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-3-11.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=11 GTEST_SHARD_INDEX=3 /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 4 of 11.
[==========] Running 1 test from 1 test suite.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadInactiveMaskReadsZero
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x000000010230fff0 llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 56
1  FeMeRuntimeCPUTests      0x000000010230de8c llvm::sys::RunSignalHandlers() + 96
2  FeMeRuntimeCPUTests      0x0000000102310af8 SignalHandler(int, __siginfo*, void*) + 328
3  libsystem_platform.dylib 0x000000018536f744 _sigtramp + 56
4  FeMeRuntimeCPUTests      0x0000000102074194 (anonymous namespace)::RuntimeCPUTest::addLoadWrapper(llvm::StringRef, llvm::StringRef) + 76
5  FeMeRuntimeCPUTests      0x00000001020755dc (anonymous namespace)::RuntimeCPUTest_TypedLoadInactiveMaskReadsZero_Test::TestBody() + 112
6  FeMeRuntimeCPUTests      0x000000010262de4c testing::Test::Run() + 228
7  FeMeRuntimeCPUTests      0x000000010262e93c testing::TestInfo::Run() + 316
8  FeMeRuntimeCPUTests      0x000000010262f124 testing::TestSuite::Run() + 884
9  FeMeRuntimeCPUTests      0x000000010263d0b4 testing::internal::UnitTestImpl::RunAllTests() + 1044
10 FeMeRuntimeCPUTests      0x000000010263cc70 testing::UnitTest::Run() + 120
11 FeMeRuntimeCPUTests      0x0000000102621910 main + 152
12 dyld                     0x0000000184fa7e00 start + 6992

--
exit: -11
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-3-11.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/4/11 (570 of 622)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/4/11' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-4-11.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=11 GTEST_SHARD_INDEX=4 /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 5 of 11.
[==========] Running 1 test from 1 test suite.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadKindMismatchIsTreatedAsOutOfBounds
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x000000010471bff0 llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 56
1  FeMeRuntimeCPUTests      0x0000000104719e8c llvm::sys::RunSignalHandlers() + 96
2  FeMeRuntimeCPUTests      0x000000010471caf8 SignalHandler(int, __siginfo*, void*) + 328
3  libsystem_platform.dylib 0x000000018536f744 _sigtramp + 56
4  FeMeRuntimeCPUTests      0x0000000104480194 (anonymous namespace)::RuntimeCPUTest::addLoadWrapper(llvm::StringRef, llvm::StringRef) + 76
5  FeMeRuntimeCPUTests      0x00000001044818ec (anonymous namespace)::RuntimeCPUTest_TypedLoadKindMismatchIsTreatedAsOutOfBounds_Test::TestBody() + 108
6  FeMeRuntimeCPUTests      0x0000000104a39e4c testing::Test::Run() + 228
7  FeMeRuntimeCPUTests      0x0000000104a3a93c testing::TestInfo::Run() + 316
8  FeMeRuntimeCPUTests      0x0000000104a3b124 testing::TestSuite::Run() + 884
9  FeMeRuntimeCPUTests      0x0000000104a490b4 testing::internal::UnitTestImpl::RunAllTests() + 1044
10 FeMeRuntimeCPUTests      0x0000000104a48c70 testing::UnitTest::Run() + 120
11 FeMeRuntimeCPUTests      0x0000000104a2d910 main + 152
12 dyld                     0x0000000184fa7e00 start + 6992

--
exit: -11
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-4-11.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/6/11 (571 of 622)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/6/11' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-6-11.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=11 GTEST_SHARD_INDEX=6 /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 7 of 11.
[==========] Running 1 test from 1 test suite.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedStoreDroppedWithoutUavFlag
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x00000001022ffff0 llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 56
1  FeMeRuntimeCPUTests      0x00000001022fde8c llvm::sys::RunSignalHandlers() + 96
2  FeMeRuntimeCPUTests      0x0000000102300af8 SignalHandler(int, __siginfo*, void*) + 328
3  libsystem_platform.dylib 0x000000018536f744 _sigtramp + 56
4  FeMeRuntimeCPUTests      0x00000001020646b4 llvm::CallInst::Create(llvm::FunctionType*, llvm::Value*, llvm::ArrayRef<llvm::Value*>, llvm::ArrayRef<llvm::OperandBundleDefT<llvm::Value*>>, llvm::Twine const&, llvm::InsertPosition) + 328
5  FeMeRuntimeCPUTests      0x00000001020644c0 llvm::IRBuilderBase::CreateCall(llvm::FunctionType*, llvm::Value*, llvm::ArrayRef<llvm::Value*>, llvm::Twine const&, llvm::MDNode*) + 80
6  FeMeRuntimeCPUTests      0x0000000102066268 (anonymous namespace)::RuntimeCPUTest::addStoreWrapper(llvm::StringRef, llvm::StringRef, llvm::Type*) + 632
7  FeMeRuntimeCPUTests      0x000000010206648c (anonymous namespace)::RuntimeCPUTest_TypedStoreDroppedWithoutUavFlag_Test::TestBody() + 136
8  FeMeRuntimeCPUTests      0x000000010261de4c testing::Test::Run() + 228
9  FeMeRuntimeCPUTests      0x000000010261e93c testing::TestInfo::Run() + 316
10 FeMeRuntimeCPUTests      0x000000010261f124 testing::TestSuite::Run() + 884
11 FeMeRuntimeCPUTests      0x000000010262d0b4 testing::internal::UnitTestImpl::RunAllTests() + 1044
12 FeMeRuntimeCPUTests      0x000000010262cc70 testing::UnitTest::Run() + 120
13 FeMeRuntimeCPUTests      0x0000000102611910 main + 152
14 dyld                     0x0000000184fa7e00 start + 6992

--
exit: -11
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-6-11.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/5/11 (572 of 622)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/5/11' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-5-11.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=11 GTEST_SHARD_INDEX=5 /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 6 of 11.
[==========] Running 1 test from 1 test suite.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedStoreRoundTrips
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x00000001047abff0 llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 56
1  FeMeRuntimeCPUTests      0x00000001047a9e8c llvm::sys::RunSignalHandlers() + 96
2  FeMeRuntimeCPUTests      0x00000001047acaf8 SignalHandler(int, __siginfo*, void*) + 328
3  libsystem_platform.dylib 0x000000018536f744 _sigtramp + 56
4  FeMeRuntimeCPUTests      0x00000001045106b4 llvm::CallInst::Create(llvm::FunctionType*, llvm::Value*, llvm::ArrayRef<llvm::Value*>, llvm::ArrayRef<llvm::OperandBundleDefT<llvm::Value*>>, llvm::Twine const&, llvm::InsertPosition) + 328
5  FeMeRuntimeCPUTests      0x00000001045104c0 llvm::IRBuilderBase::CreateCall(llvm::FunctionType*, llvm::Value*, llvm::ArrayRef<llvm::Value*>, llvm::Twine const&, llvm::MDNode*) + 80
6  FeMeRuntimeCPUTests      0x0000000104512268 (anonymous namespace)::RuntimeCPUTest::addStoreWrapper(llvm::StringRef, llvm::StringRef, llvm::Type*) + 632
7  FeMeRuntimeCPUTests      0x0000000104511c14 (anonymous namespace)::RuntimeCPUTest_TypedStoreRoundTrips_Test::TestBody() + 132
8  FeMeRuntimeCPUTests      0x0000000104ac9e4c testing::Test::Run() + 228
9  FeMeRuntimeCPUTests      0x0000000104aca93c testing::TestInfo::Run() + 316
10 FeMeRuntimeCPUTests      0x0000000104acb124 testing::TestSuite::Run() + 884
11 FeMeRuntimeCPUTests      0x0000000104ad90b4 testing::internal::UnitTestImpl::RunAllTests() + 1044
12 FeMeRuntimeCPUTests      0x0000000104ad8c70 testing::UnitTest::Run() + 120
13 FeMeRuntimeCPUTests      0x0000000104abd910 main + 152
14 dyld                     0x0000000184fa7e00 start + 6992

--
exit: -11
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-5-11.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/9/11 (578 of 622)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/9/11' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-9-11.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=11 GTEST_SHARD_INDEX=9 /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 10 of 11.
[==========] Running 1 test from 1 test suite.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.RawLoadStoreRoundTrip
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x000000010086fff0 llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 56
1  FeMeRuntimeCPUTests      0x000000010086de8c llvm::sys::RunSignalHandlers() + 96
2  FeMeRuntimeCPUTests      0x0000000100870af8 SignalHandler(int, __siginfo*, void*) + 328
3  libsystem_platform.dylib 0x000000018536f744 _sigtramp + 56
4  FeMeRuntimeCPUTests      0x00000001005d46b4 llvm::CallInst::Create(llvm::FunctionType*, llvm::Value*, llvm::ArrayRef<llvm::Value*>, llvm::ArrayRef<llvm::OperandBundleDefT<llvm::Value*>>, llvm::Twine const&, llvm::InsertPosition) + 328
5  FeMeRuntimeCPUTests      0x00000001005d44c0 llvm::IRBuilderBase::CreateCall(llvm::FunctionType*, llvm::Value*, llvm::ArrayRef<llvm::Value*>, llvm::Twine const&, llvm::MDNode*) + 80
6  FeMeRuntimeCPUTests      0x00000001005d6268 (anonymous namespace)::RuntimeCPUTest::addStoreWrapper(llvm::StringRef, llvm::StringRef, llvm::Type*) + 632
7  FeMeRuntimeCPUTests      0x00000001005d6e84 (anonymous namespace)::RuntimeCPUTest_RawLoadStoreRoundTrip_Test::TestBody() + 120
8  FeMeRuntimeCPUTests      0x0000000100b8de4c testing::Test::Run() + 228
9  FeMeRuntimeCPUTests      0x0000000100b8e93c testing::TestInfo::Run() + 316
10 FeMeRuntimeCPUTests      0x0000000100b8f124 testing::TestSuite::Run() + 884
11 FeMeRuntimeCPUTests      0x0000000100b9d0b4 testing::internal::UnitTestImpl::RunAllTests() + 1044
12 FeMeRuntimeCPUTests      0x0000000100b9cc70 testing::UnitTest::Run() + 120
13 FeMeRuntimeCPUTests      0x0000000100b81910 main + 152
14 dyld                     0x0000000184fa7e00 start + 6992

--
exit: -11
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-9-11.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/8/11 (579 of 622)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/8/11' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-8-11.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=11 GTEST_SHARD_INDEX=8 /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 9 of 11.
[==========] Running 1 test from 1 test suite.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TrustedFlagSkipsOffsetCheck
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x000000010041bff0 llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 56
1  FeMeRuntimeCPUTests      0x0000000100419e8c llvm::sys::RunSignalHandlers() + 96
2  FeMeRuntimeCPUTests      0x000000010041caf8 SignalHandler(int, __siginfo*, void*) + 328
3  libsystem_platform.dylib 0x000000018536f744 _sigtramp + 56
4  FeMeRuntimeCPUTests      0x0000000100180194 (anonymous namespace)::RuntimeCPUTest::addLoadWrapper(llvm::StringRef, llvm::StringRef) + 76
5  FeMeRuntimeCPUTests      0x0000000100182abc (anonymous namespace)::RuntimeCPUTest_TrustedFlagSkipsOffsetCheck_Test::TestBody() + 120
6  FeMeRuntimeCPUTests      0x0000000100739e4c testing::Test::Run() + 228
7  FeMeRuntimeCPUTests      0x000000010073a93c testing::TestInfo::Run() + 316
8  FeMeRuntimeCPUTests      0x000000010073b124 testing::TestSuite::Run() + 884
9  FeMeRuntimeCPUTests      0x00000001007490b4 testing::internal::UnitTestImpl::RunAllTests() + 1044
10 FeMeRuntimeCPUTests      0x0000000100748c70 testing::UnitTest::Run() + 120
11 FeMeRuntimeCPUTests      0x000000010072d910 main + 152
12 dyld                     0x0000000184fa7e00 start + 6992

--
exit: -11
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-8-11.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/7/11 (584 of 622)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/7/11' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-7-11.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=11 GTEST_SHARD_INDEX=7 /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 8 of 11.
[==========] Running 1 test from 1 test suite.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadOutOfRangeOffsetReadsZero
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x00000001008efff0 llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 56
1  FeMeRuntimeCPUTests      0x00000001008ede8c llvm::sys::RunSignalHandlers() + 96
2  FeMeRuntimeCPUTests      0x00000001008f0af8 SignalHandler(int, __siginfo*, void*) + 328
3  libsystem_platform.dylib 0x000000018536f744 _sigtramp + 56
4  FeMeRuntimeCPUTests      0x0000000100654194 (anonymous namespace)::RuntimeCPUTest::addLoadWrapper(llvm::StringRef, llvm::StringRef) + 76
5  FeMeRuntimeCPUTests      0x00000001006567a0 (anonymous namespace)::RuntimeCPUTest_TypedLoadOutOfRangeOffsetReadsZero_Test::TestBody() + 112
6  FeMeRuntimeCPUTests      0x0000000100c0de4c testing::Test::Run() + 228
7  FeMeRuntimeCPUTests      0x0000000100c0e93c testing::TestInfo::Run() + 316
8  FeMeRuntimeCPUTests      0x0000000100c0f124 testing::TestSuite::Run() + 884
9  FeMeRuntimeCPUTests      0x0000000100c1d0b4 testing::internal::UnitTestImpl::RunAllTests() + 1044
10 FeMeRuntimeCPUTests      0x0000000100c1cc70 testing::UnitTest::Run() + 120
11 FeMeRuntimeCPUTests      0x0000000100c01910 main + 152
12 dyld                     0x0000000184fa7e00 start + 6992

--
exit: -11
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-17487-7-11.json
********************
FAIL: FEME :: Runtime/CPU/runtime-cpu-bitcode.test (620 of 622)
******************** TEST 'FEME :: Runtime/CPU/runtime-cpu-bitcode.test' FAILED ********************
Exit Code: 1

Command Output (stdout):
--
# RUN: at line 1
/Users/cbieneman/dev/llvm-project/build-rel/bin/clang -c -emit-llvm -O2 -ffreestanding -fno-builtin      /Users/cbieneman/dev/llvm-project/feme/test/Runtime/CPU/../../../runtime/CPU/FeMeRuntimeCPU.c -o /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/test/Runtime/CPU/Output/runtime-cpu-bitcode.test.tmp.bc
# executed command: /Users/cbieneman/dev/llvm-project/build-rel/bin/clang -c -emit-llvm -O2 -ffreestanding -fno-builtin /Users/cbieneman/dev/llvm-project/feme/test/Runtime/CPU/../../../runtime/CPU/FeMeRuntimeCPU.c -o /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/test/Runtime/CPU/Output/runtime-cpu-bitcode.test.tmp.bc
# RUN: at line 3
/Users/cbieneman/dev/llvm-project/build-rel/bin/opt -passes=verify -disable-output /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/test/Runtime/CPU/Output/runtime-cpu-bitcode.test.tmp.bc
# executed command: /Users/cbieneman/dev/llvm-project/build-rel/bin/opt -passes=verify -disable-output /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/test/Runtime/CPU/Output/runtime-cpu-bitcode.test.tmp.bc
# RUN: at line 4
/Users/cbieneman/dev/llvm-project/build-rel/bin/opt -S /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/test/Runtime/CPU/Output/runtime-cpu-bitcode.test.tmp.bc -o - | /Users/cbieneman/dev/llvm-project/build-rel/bin/FileCheck /Users/cbieneman/dev/llvm-project/feme/test/Runtime/CPU/runtime-cpu-bitcode.test
# executed command: /Users/cbieneman/dev/llvm-project/build-rel/bin/opt -S /Users/cbieneman/dev/llvm-project/build-rel/tools/feme/test/Runtime/CPU/Output/runtime-cpu-bitcode.test.tmp.bc -o -
# executed command: /Users/cbieneman/dev/llvm-project/build-rel/bin/FileCheck /Users/cbieneman/dev/llvm-project/feme/test/Runtime/CPU/runtime-cpu-bitcode.test
# .---command stderr------------
# | /Users/cbieneman/dev/llvm-project/feme/test/Runtime/CPU/runtime-cpu-bitcode.test:14:14: error: CHECK-DAG: expected string not found in input
# | ; CHECK-DAG: <4 x float> @feme.cpu.resource.load.typed.v4f32(
# |              ^
# | <stdin>:1:1: note: scanning from here
# | ; ModuleID = '/Users/cbieneman/dev/llvm-project/build-rel/tools/feme/test/Runtime/CPU/Output/runtime-cpu-bitcode.test.tmp.bc'
# | ^
# | <stdin>:7:8: note: possible intended match here
# | define <4 x float> @"\01feme.cpu.resource.load.typed.v4f32"(ptr nofree noundef readonly captures(none) %Heap, i32 noundef %HeapCount, i32 noundef %DescriptorIndex, i64 noundef %ElementIndex, i1 noundef zeroext %Mask) local_unnamed_addr #0 {
# |        ^
# |
# | Input file: <stdin>
# | Check file: /Users/cbieneman/dev/llvm-project/feme/test/Runtime/CPU/runtime-cpu-bitcode.test
# |
# | -dump-input=help explains the following input dump.
# |
# | Input was:
# | <<<<<<
# |           1: ; ModuleID = '/Users/cbieneman/dev/llvm-project/build-rel/tools/feme/test/Runtime/CPU/Output/runtime-cpu-bitcode.test.tmp.bc'
# | dag:14'0    {                                                                                                                                search range start (exclusive)
# | dag:14'1                                                                                                                                     error: no match found in search range
# |           2: source_filename = "/Users/cbieneman/dev/llvm-project/feme/test/Runtime/CPU/../../../runtime/CPU/FeMeRuntimeCPU.c"
# |           3: target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
# |           4: target triple = "arm64-apple-macosx26.0.0"
# |           5:
# |           6: ; Function Attrs: alwaysinline mustprogress nofree norecurse nosync nounwind ssp willreturn memory(read, inaccessiblemem: none, target_mem: none)
# |           7: define <4 x float> @"\01feme.cpu.resource.load.typed.v4f32"(ptr nofree noundef readonly captures(none) %Heap, i32 noundef %HeapCount, i32 noundef %DescriptorIndex, i64 noundef %ElementIndex, i1 noundef zeroext %Mask) local_unnamed_addr #0 {
# | dag:14'2            ?                                                                                                                                                                                                                                           possible intended match
# |           8: entry:
# |           9:  %cmp.not.i = icmp ult i32 %DescriptorIndex, %HeapCount
# |          10:  br i1 %cmp.not.i, label %femeRTLoadDescriptor.exit, label %cleanup11
# |          11:
# |          12: femeRTLoadDescriptor.exit: ; preds = %entry
# |           .
# |           .
# |           .
# |         339: !29 = !{!30}
# |         340: !30 = distinct !{!30, !31, !"femeRTLoadDescriptor: %agg.result"}
# |         341: !31 = distinct !{!31, !"femeRTLoadDescriptor"}
# |         342: !32 = !{!33}
# |         343: !33 = distinct !{!33, !34, !"femeRTLoadDescriptor: %agg.result"}
# |         344: !34 = distinct !{!34, !"femeRTLoadDescriptor"}
# | dag:14'3                                                    } search range end (exclusive)
# | >>>>>>
# `-----------------------------
# error: command failed with exit status: 1

--

********************
********************
Failed Tests (12):
  FEME :: Runtime/CPU/runtime-cpu-bitcode.test
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/0/11
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/1/11
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/10/11
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/2/11
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/3/11
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/4/11
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/5/11
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/6/11
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/7/11
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/8/11
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/9/11


Testing Time: 10.90s

Total Discovered Tests: 673
  Passed: 661 (98.22%)
  Failed:  12 (1.78%)
