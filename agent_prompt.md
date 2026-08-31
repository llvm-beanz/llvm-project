---
model: claude-sonnet-5
resume: 50bf9c01-6e85-44df-8b7a-5c13ed0b05e1
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

Also please run the Vulkan CTS from the checkout under /home/dev/dev/VK-GL-CTS/
after each change and update the VulkanCTSReport.md. Please keep the
Vulkan14FeatureInventory and VulkanExtensionInventory up to date with each
change as well.

If the request is to complete a roadmap stage, if you complete it please strike
it through on the roadmap document, if you do not, please add entries to the
roadmap document to break down the remaining work for that milestone.

During the H6 milestone breakdowns things have gone a little crazy with nesting
letters in strange ways. Please avoid nesting milestones more than one lowercase
letter deep going forward.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

A bunch of FeMe's tests are failing due to undefined behavior. You should be
able to reproduce by building with UBSan enabled. Can you please fix these?

The failures I'm seeing are:

```
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/7/22 (1 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/7/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-7-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=7 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 8 of 22.
[==========] Running 8 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 2 tests from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedStoreDroppedWithoutUavFlag
[       OK ] RuntimeCPUTest.TypedStoreDroppedWithoutUavFlag (35596 ms)
[ RUN      ] RuntimeCPUTest.RawLoadStoreRoundTripV3I32
[       OK ] RuntimeCPUTest.RawLoadStoreRoundTripV3I32 (37423 ms)
[----------] 2 tests from RuntimeCPUTest (73020 ms total)

[----------] 6 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.LoadFetchesPartialComponentFloatFormats
[       OK ] ImageSamplingTest.LoadFetchesPartialComponentFloatFormats (37790 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesD16Unorm
[       OK ] ImageSamplingTest.LoadFetchesD16Unorm (37906 ms)
[ RUN      ] ImageSamplingTest.LoadI32OutOfRangeCoordinateReadsZero
[       OK ] ImageSamplingTest.LoadI32OutOfRangeCoordinateReadsZero (39520 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR8Sint
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x0000000105feabfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x0000000105febf74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x0000000105fe557c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x0000000105feffb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x000000010485b964 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR8Sint_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x00000001073acd00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x000000010732b4e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x000000010732b394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x000000010732cfcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x000000010732f678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x0000000107346f50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x00000001073c43dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x0000000107346488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x00000001073462dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x00000001072fe16c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x00000001072fe118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7180==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016b62e160 sp 0x00016b62dfd0 T51671)
==7180==Hint: pc points to the zero page.
==7180==The signal is caused by a READ memory access.
==7180==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x0001073accfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x00010732b4e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x00010732b390 in testing::Test::Run() gtest.cc:2688
    #4 0x00010732cfc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x00010732f674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x000107346f4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x0001073c43d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x000107346484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x0001073462d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x0001072fe168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x0001072fe114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7180==Register values:
 x[0] = 0x000000016b62e0f8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017c23e0804ff000  x[13] = 0x017c13dd004fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d000cd200c000  x[17] = 0x00000007c13dd400  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016b62ef68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016b62f0d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016b62e160     lr = 0x000000010485b964     sp = 0x000000016b62dfd0
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7180==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-7-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/13/22 (2 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/13/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-13-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=13 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 14 of 22.
[==========] Running 8 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadPackedR8G8B8A8Uint
[       OK ] RuntimeCPUTest.TypedLoadPackedR8G8B8A8Uint (35959 ms)
[----------] 1 test from RuntimeCPUTest (35959 ms total)

[----------] 7 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.ImplicitLodMinifyingUsesMinFilterNotMagFilter
[       OK ] ImageSamplingTest.ImplicitLodMinifyingUsesMinFilterNotMagFilter (37836 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR11G11B10FloatNonZeroValues
[       OK ] ImageSamplingTest.LoadFetchesR11G11B10FloatNonZeroValues (37748 ms)
[ RUN      ] ImageSamplingTest.LoadI32FetchesExplicitSampleOfMultisampledTexel
[       OK ] ImageSamplingTest.LoadI32FetchesExplicitSampleOfMultisampledTexel (38281 ms)
[ RUN      ] ImageSamplingTest.Load2DArrayI32ReadsRequestedLayer
[       OK ] ImageSamplingTest.Load2DArrayI32ReadsRequestedLayer (39405 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR16UnormQuantized
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x00000001067cebfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x00000001067cff74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x00000001067c957c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x00000001067d3fb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x00000001050428bc (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR16UnormQuantized_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x0000000107b90d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x0000000107b0f4e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x0000000107b0f394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000107b10fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x0000000107b13678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x0000000107b2af50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x0000000107ba83dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x0000000107b2a488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x0000000107b2a2dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x0000000107ae216c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x0000000107ae2118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7168==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016ae4a160 sp 0x00016ae49fd0 T51647)
==7168==Hint: pc points to the zero page.
==7168==The signal is caused by a READ memory access.
==7168==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x000107b90cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x000107b0f4e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x000107b0f390 in testing::Test::Run() gtest.cc:2688
    #4 0x000107b10fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x000107b13674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x000107b2af4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x000107ba83d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x000107b2a484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x000107b2a2d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x000107ae2168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x000107ae2114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7168==Register values:
 x[0] = 0x000000016ae4a0f8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017be3de804ff000  x[13] = 0x017bd3db004fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d0004fa00c000  x[17] = 0x00000007bd3db400  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016ae4af68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016ae4b0d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016ae4a160     lr = 0x00000001050428bc     sp = 0x000000016ae49fd0
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7168==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-13-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/0/22 (3 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/0/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-0-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=0 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 1 of 22.
[==========] Running 8 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 2 tests from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadIdentityFormat
[       OK ] RuntimeCPUTest.TypedLoadIdentityFormat (35579 ms)
[ RUN      ] RuntimeCPUTest.RawLoadV4F32InactiveMaskReadsZero
[       OK ] RuntimeCPUTest.RawLoadV4F32InactiveMaskReadsZero (37540 ms)
[----------] 2 tests from RuntimeCPUTest (73120 ms total)

[----------] 6 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.ImplicitLodSelectsCoarserMipFromDerivatives
[       OK ] ImageSamplingTest.ImplicitLodSelectsCoarserMipFromDerivatives (37553 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR16Unorm
[       OK ] ImageSamplingTest.LoadFetchesR16Unorm (38396 ms)
[ RUN      ] ImageSamplingTest.LoadI32FetchesR16Sint
[       OK ] ImageSamplingTest.LoadI32FetchesR16Sint (39399 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR16G16B16A16Uint
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x000000010457abfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x000000010457bf74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x000000010457557c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x000000010457ffb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x0000000102de6184 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR16G16B16A16Uint_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x000000010593cd00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x00000001058bb4e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x00000001058bb394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x00000001058bcfcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x00000001058bf678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x00000001058d6f50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x00000001059543dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x00000001058d6488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x00000001058d62dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x000000010588e16c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x000000010588e118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7169==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016d09e160 sp 0x00016d09dea0 T51649)
==7169==Hint: pc points to the zero page.
==7169==The signal is caused by a READ memory access.
==7169==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x00010593ccfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x0001058bb4e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x0001058bb390 in testing::Test::Run() gtest.cc:2688
    #4 0x0001058bcfc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x0001058bf674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x0001058d6f4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x0001059543d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x0001058d6484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x0001058d62d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x00010588e168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x00010588e114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7169==Register values:
 x[0] = 0x000000016d09e0f0   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017bf3df004ff000  x[13] = 0x017be3db804fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d0004ee00c000  x[17] = 0x00000007be3dbc00  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016d09ef68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016d09f0d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016d09e160     lr = 0x0000000102de6184     sp = 0x000000016d09dea0
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7169==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-0-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/16/22 (4 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/16/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-16-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=16 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 17 of 22.
[==========] Running 7 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedStorePackedR8G8B8A8SintRoundTrips
[       OK ] RuntimeCPUTest.TypedStorePackedR8G8B8A8SintRoundTrips (35774 ms)
[----------] 1 test from RuntimeCPUTest (35774 ms total)

[----------] 6 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.ImplicitLodTrilinearBlendsBetweenTwoMipLevels
[       OK ] ImageSamplingTest.ImplicitLodTrilinearBlendsBetweenTwoMipLevels (37680 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR16G16B16A16Snorm
[       OK ] ImageSamplingTest.LoadFetchesR16G16B16A16Snorm (37875 ms)
[ RUN      ] ImageSamplingTest.LoadI32FetchesR16G16B16A16Uint
[       OK ] ImageSamplingTest.LoadI32FetchesR16G16B16A16Uint (38235 ms)
[ RUN      ] ImageSamplingTest.SampleCubeArraySelectsRequestedCubeElement
[       OK ] ImageSamplingTest.SampleCubeArraySelectsRequestedCubeElement (39518 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR16Sint
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x0000000101926bfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x0000000101927f74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x000000010192157c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x000000010192bfb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x000000010019bd10 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR16Sint_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x0000000102ce8d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x0000000102c674e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x0000000102c67394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000102c68fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x0000000102c6b678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x0000000102c82f50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x0000000102d003dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x0000000102c82488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x0000000102c822dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x0000000102c3a16c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x0000000102c3a118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7173==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016fcf2160 sp 0x00016fcf1fd0 T51657)
==7173==Hint: pc points to the zero page.
==7173==The signal is caused by a READ memory access.
==7173==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x000102ce8cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x000102c674e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x000102c67390 in testing::Test::Run() gtest.cc:2688
    #4 0x000102c68fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x000102c6b674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x000102c82f4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x000102d003d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x000102c82484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x000102c822d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x000102c3a168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x000102c3a114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7173==Register values:
 x[0] = 0x000000016fcf20f8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017bf3df004ff000  x[13] = 0x017be3db804fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d000cf800c000  x[17] = 0x00000007be3dbc00  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016fcf2f68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016fcf30d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016fcf2160     lr = 0x000000010019bd10     sp = 0x000000016fcf1fd0
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7173==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-16-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/6/22 (5 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/6/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-6-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=6 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 7 of 22.
[==========] Running 8 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 2 tests from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedStoreRoundTrips
[       OK ] RuntimeCPUTest.TypedStoreRoundTrips (35989 ms)
[ RUN      ] RuntimeCPUTest.RawLoadStoreRoundTripV2I32
[       OK ] RuntimeCPUTest.RawLoadStoreRoundTripV2I32 (37439 ms)
[----------] 2 tests from RuntimeCPUTest (73429 ms total)

[----------] 6 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.ExplicitLoadFetchesExactTexel
[       OK ] ImageSamplingTest.ExplicitLoadFetchesExactTexel (37700 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesA1B5G5R5Unorm
[       OK ] ImageSamplingTest.LoadFetchesA1B5G5R5Unorm (37827 ms)
[ RUN      ] ImageSamplingTest.LoadI32FetchesR10G10B10A2Sint
[       OK ] ImageSamplingTest.LoadI32FetchesR10G10B10A2Sint (39535 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR8Uint
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x0000000103996bfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x0000000103997f74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x000000010399157c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x000000010399bfb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x00000001022072a0 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR8Uint_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x0000000104d58d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x0000000104cd74e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x0000000104cd7394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000104cd8fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x0000000104cdb678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x0000000104cf2f50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x0000000104d703dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x0000000104cf2488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x0000000104cf22dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x0000000104caa16c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x0000000104caa118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7179==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016dc82160 sp 0x00016dc81fd0 T51669)
==7179==Hint: pc points to the zero page.
==7179==The signal is caused by a READ memory access.
==7179==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x000104d58cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x000104cd74e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x000104cd7390 in testing::Test::Run() gtest.cc:2688
    #4 0x000104cd8fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x000104cdb674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x000104cf2f4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x000104d703d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x000104cf2484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x000104cf22d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x000104caa168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x000104caa114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7179==Register values:
 x[0] = 0x000000016dc820f8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017c03df804ff000  x[13] = 0x017bf3dc004fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d000644008000  x[17] = 0x00000007bf3dc400  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016dc82f68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016dc830d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016dc82160     lr = 0x00000001022072a0     sp = 0x000000016dc81fd0
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7179==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-6-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/11/22 (6 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/11/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-11-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=11 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 12 of 22.
[==========] Running 8 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedStoreV4I32RoundTrips
[       OK ] RuntimeCPUTest.TypedStoreV4I32RoundTrips (35765 ms)
[----------] 1 test from RuntimeCPUTest (35765 ms total)

[----------] 7 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.ExplicitLodMinifyingUsesMinFilterNotMagFilter
[       OK ] ImageSamplingTest.ExplicitLodMinifyingUsesMinFilterNotMagFilter (37860 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR10G10B10A2Snorm
[       OK ] ImageSamplingTest.LoadFetchesR10G10B10A2Snorm (37881 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesExplicitSampleOfMultisampledTexel
[       OK ] ImageSamplingTest.LoadFetchesExplicitSampleOfMultisampledTexel (38116 ms)
[ RUN      ] ImageSamplingTest.Load2DArrayReadsRequestedLayer
[       OK ] ImageSamplingTest.Load2DArrayReadsRequestedLayer (39554 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR8G8Sint
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x00000001020e2bfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x00000001020e3f74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x00000001020dd57c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x00000001020e7fb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x0000000100955984 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR8G8Sint_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x00000001034a4d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x00000001034234e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x0000000103423394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000103424fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x0000000103427678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x000000010343ef50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x00000001034bc3dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x000000010343e488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x000000010343e2dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x00000001033f616c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x00000001033f6118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7170==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016f536160 sp 0x00016f535f80 T51652)
==7170==Hint: pc points to the zero page.
==7170==The signal is caused by a READ memory access.
==7170==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x0001034a4cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x0001034234e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x000103423390 in testing::Test::Run() gtest.cc:2688
    #4 0x000103424fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x000103427674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x00010343ef4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x0001034bc3d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x00010343e484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x00010343e2d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x0001033f6168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x0001033f6114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7170==Register values:
 x[0] = 0x000000016f5360f8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017be3de804ff000  x[13] = 0x017bd3db004fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d000cfc00c000  x[17] = 0x00000007bd3db400  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016f536f68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016f5370d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016f536160     lr = 0x0000000100955984     sp = 0x000000016f535f80
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7170==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-11-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/14/22 (7 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/14/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-14-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=14 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 15 of 22.
[==========] Running 8 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadPackedR8G8B8A8Sint
[       OK ] RuntimeCPUTest.TypedLoadPackedR8G8B8A8Sint (35863 ms)
[----------] 1 test from RuntimeCPUTest (35863 ms total)

[----------] 7 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.ExplicitLodTrilinearBlendsBetweenTwoMipLevels
[       OK ] ImageSamplingTest.ExplicitLodTrilinearBlendsBetweenTwoMipLevels (37523 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR16G16B16A16Float
[       OK ] ImageSamplingTest.LoadFetchesR16G16B16A16Float (37933 ms)
[ RUN      ] ImageSamplingTest.LoadI32FetchesIdentityFormat
[       OK ] ImageSamplingTest.LoadI32FetchesIdentityFormat (38151 ms)
[ RUN      ] ImageSamplingTest.Load2DArrayI32ReadsRequestedLayerAndSample
[       OK ] ImageSamplingTest.Load2DArrayI32ReadsRequestedLayerAndSample (39271 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR16SnormQuantized
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x0000000101896bfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x0000000101897f74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x000000010189157c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x000000010189bfb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x000000010010af80 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR16SnormQuantized_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x0000000102c58d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x0000000102bd74e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x0000000102bd7394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000102bd8fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x0000000102bdb678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x0000000102bf2f50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x0000000102c703dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x0000000102bf2488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x0000000102bf22dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x0000000102baa16c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x0000000102baa118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7171==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016fd82160 sp 0x00016fd81fd0 T51655)
==7171==Hint: pc points to the zero page.
==7171==The signal is caused by a READ memory access.
==7171==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x000102c58cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x000102bd74e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x000102bd7390 in testing::Test::Run() gtest.cc:2688
    #4 0x000102bd8fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x000102bdb674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x000102bf2f4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x000102c703d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x000102bf2484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x000102bf22d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x000102baa168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x000102baa114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7171==Register values:
 x[0] = 0x000000016fd820f8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017bf3df004ff000  x[13] = 0x017be3db804fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d000d00004000  x[17] = 0x00000007be3dbc00  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016fd82f68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016fd830d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016fd82160     lr = 0x000000010010af80     sp = 0x000000016fd81fd0
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7171==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-14-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/10/22 (8 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/10/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-10-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=10 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 11 of 22.
[==========] Running 8 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadV4I32InactiveMaskReadsZero
[       OK ] RuntimeCPUTest.TypedLoadV4I32InactiveMaskReadsZero (36021 ms)
[----------] 1 test from RuntimeCPUTest (36021 ms total)

[----------] 7 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.LinearSampleBlendsFourTexels
[       OK ] ImageSamplingTest.LinearSampleBlendsFourTexels (37435 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR10G10B10A2Unorm
[       OK ] ImageSamplingTest.LoadFetchesR10G10B10A2Unorm (38059 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesSample0OfMultisampledTexel
[       OK ] ImageSamplingTest.LoadFetchesSample0OfMultisampledTexel (38149 ms)
[ RUN      ] ImageSamplingTest.Sample2DArrayRoundsLayerToNearest
[       OK ] ImageSamplingTest.Sample2DArrayRoundsLayerToNearest (39504 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR8G8Uint
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x0000000105982bfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x0000000105983f74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x000000010597d57c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x0000000105987fb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x00000001041f5110 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR8G8Uint_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x0000000106d44d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x0000000106cc34e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x0000000106cc3394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000106cc4fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x0000000106cc7678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x0000000106cdef50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x0000000106d5c3dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x0000000106cde488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x0000000106cde2dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x0000000106c9616c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x0000000106c96118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7174==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016bc96160 sp 0x00016bc95f80 T51659)
==7174==Hint: pc points to the zero page.
==7174==The signal is caused by a READ memory access.
==7174==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x000106d44cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x000106cc34e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x000106cc3390 in testing::Test::Run() gtest.cc:2688
    #4 0x000106cc4fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x000106cc7674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x000106cdef4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x000106d5c3d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x000106cde484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x000106cde2d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x000106c96168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x000106c96114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7174==Register values:
 x[0] = 0x000000016bc960f8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017bd3de004ff000  x[13] = 0x017bc3da804fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d00066a404000  x[17] = 0x00000007bc3dac00  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016bc96f68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016bc970d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016bc96160     lr = 0x00000001041f5110     sp = 0x000000016bc95f80
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7174==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-10-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/1/22 (9 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/1/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-1-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=1 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 2 of 22.
[==========] Running 8 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 2 tests from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadPackedR8G8B8A8Unorm
[       OK ] RuntimeCPUTest.TypedLoadPackedR8G8B8A8Unorm (35819 ms)
[ RUN      ] RuntimeCPUTest.RawStoreV4F32RoundTrips
[       OK ] RuntimeCPUTest.RawStoreV4F32RoundTrips (37762 ms)
[----------] 2 tests from RuntimeCPUTest (73582 ms total)

[----------] 6 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.MaxLodClampsImplicitSampleToBaseLevel
[       OK ] ImageSamplingTest.MaxLodClampsImplicitSampleToBaseLevel (37444 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR16Snorm
[       OK ] ImageSamplingTest.LoadFetchesR16Snorm (38162 ms)
[ RUN      ] ImageSamplingTest.LoadI32FetchesR16G16Uint
[       OK ] ImageSamplingTest.LoadI32FetchesR16G16Uint (39414 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR16G16B16A16Sint
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x0000000105e46bfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x0000000105e47f74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x0000000105e4157c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x0000000105e4bfb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x00000001046b2d58 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR16G16B16A16Sint_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x0000000107208d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x00000001071874e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x0000000107187394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000107188fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x000000010718b678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x00000001071a2f50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x00000001072203dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x00000001071a2488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x00000001071a22dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x000000010715a16c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x000000010715a118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7175==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016b7d2160 sp 0x00016b7d1ea0 T51661)
==7175==Hint: pc points to the zero page.
==7175==The signal is caused by a READ memory access.
==7175==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x000107208cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x0001071874e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x000107187390 in testing::Test::Run() gtest.cc:2688
    #4 0x000107188fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x00010718b674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x0001071a2f4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x0001072203d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x0001071a2484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x0001071a22d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x00010715a168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x00010715a114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7175==Register values:
 x[0] = 0x000000016b7d20f0   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017bf3df004ff000  x[13] = 0x017be3db804fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d00043600c000  x[17] = 0x00000007be3dbc00  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016b7d2f68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016b7d30d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016b7d2160     lr = 0x00000001046b2d58     sp = 0x000000016b7d1ea0
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7175==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-1-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/5/22 (10 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/5/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-5-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=5 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 6 of 22.
[==========] Running 8 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 2 tests from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadKindMismatchIsTreatedAsOutOfBounds
[       OK ] RuntimeCPUTest.TypedLoadKindMismatchIsTreatedAsOutOfBounds (35638 ms)
[ RUN      ] RuntimeCPUTest.RawLoadStoreRoundTripV3F32
[       OK ] RuntimeCPUTest.RawLoadStoreRoundTripV3F32 (37595 ms)
[----------] 2 tests from RuntimeCPUTest (73233 ms total)

[----------] 6 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.ComparisonSamplingLessEqualPasses
[       OK ] ImageSamplingTest.ComparisonSamplingLessEqualPasses (37748 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesA8Unorm
[       OK ] ImageSamplingTest.LoadFetchesA8Unorm (38227 ms)
[ RUN      ] ImageSamplingTest.LoadI32FetchesR10G10B10A2Uint
[       OK ] ImageSamplingTest.LoadI32FetchesR10G10B10A2Uint (39774 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR8SnormQuantized
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x0000000101b72bfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x0000000101b73f74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x0000000101b6d57c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x0000000101b77fb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x00000001003e2644 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR8SnormQuantized_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x0000000102f34d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x0000000102eb34e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x0000000102eb3394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000102eb4fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x0000000102eb7678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x0000000102ecef50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x0000000102f4c3dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x0000000102ece488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x0000000102ece2dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x0000000102e8616c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x0000000102e86118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7178==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016faa6160 sp 0x00016faa5fd0 T51667)
==7178==Hint: pc points to the zero page.
==7178==The signal is caused by a READ memory access.
==7178==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x000102f34cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x000102eb34e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x000102eb3390 in testing::Test::Run() gtest.cc:2688
    #4 0x000102eb4fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x000102eb7674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x000102ecef4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x000102f4c3d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x000102ece484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x000102ece2d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x000102e86168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x000102e86114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7178==Register values:
 x[0] = 0x000000016faa60f8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017c03df804ff000  x[13] = 0x017bf3dc004fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d00068200c000  x[17] = 0x00000007bf3dc400  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016faa6f68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016faa70d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016faa6160     lr = 0x00000001003e2644     sp = 0x000000016faa5fd0
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7178==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-5-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/8/22 (11 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/8/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-8-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=8 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 9 of 22.
[==========] Running 8 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 2 tests from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedStoreSnormRoundTrips
[       OK ] RuntimeCPUTest.TypedStoreSnormRoundTrips (35974 ms)
[ RUN      ] RuntimeCPUTest.RawLoadStoreRoundTripV4I32
[       OK ] RuntimeCPUTest.RawLoadStoreRoundTripV4I32 (37607 ms)
[----------] 2 tests from RuntimeCPUTest (73581 ms total)

[----------] 6 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.LoadFetchesR8G8B8A8Snorm
[       OK ] ImageSamplingTest.LoadFetchesR8G8B8A8Snorm (37620 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesD32Float
[       OK ] ImageSamplingTest.LoadFetchesD32Float (38118 ms)
[ RUN      ] ImageSamplingTest.LoadI32InactiveLaneReadsZero
[       OK ] ImageSamplingTest.LoadI32InactiveLaneReadsZero (39098 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR8G8UnormQuantized
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x0000000103f42bfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x0000000103f43f74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x0000000103f3d57c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x0000000103f47fb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x00000001027b4028 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR8G8UnormQuantized_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x0000000105304d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x00000001052834e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x0000000105283394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000105284fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x0000000105287678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x000000010529ef50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x000000010531c3dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x000000010529e488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x000000010529e2dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x000000010525616c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x0000000105256118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7181==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016d6d6160 sp 0x00016d6d5f80 T51673)
==7181==Hint: pc points to the zero page.
==7181==The signal is caused by a READ memory access.
==7181==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x000105304cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x0001052834e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x000105283390 in testing::Test::Run() gtest.cc:2688
    #4 0x000105284fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x000105287674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x00010529ef4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x00010531c3d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x00010529e484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x00010529e2d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x000105256168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x000105256114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7181==Register values:
 x[0] = 0x000000016d6d60f8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017c33e1004ff000  x[13] = 0x017c23dd804fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d000d2e00c000  x[17] = 0x00000007c23ddc00  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016d6d6f68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016d6d70d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016d6d6160     lr = 0x00000001027b4028     sp = 0x000000016d6d5f80
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7181==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-8-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/15/22 (12 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/15/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-15-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=15 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 16 of 22.
[==========] Running 8 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedStorePackedR8G8B8A8UintRoundTrips
[       OK ] RuntimeCPUTest.TypedStorePackedR8G8B8A8UintRoundTrips (35948 ms)
[----------] 1 test from RuntimeCPUTest (35948 ms total)

[----------] 7 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.ExplicitLodNearestMipFilterStillRoundsToOneLevel
[       OK ] ImageSamplingTest.ExplicitLodNearestMipFilterStillRoundsToOneLevel (37464 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR16G16B16A16Unorm
[       OK ] ImageSamplingTest.LoadFetchesR16G16B16A16Unorm (37673 ms)
[ RUN      ] ImageSamplingTest.LoadI32FetchesR8G8B8A8Sint
[       OK ] ImageSamplingTest.LoadI32FetchesR8G8B8A8Sint (37983 ms)
[ RUN      ] ImageSamplingTest.SampleCubeSelectsEachFaceByDirection
[       OK ] ImageSamplingTest.SampleCubeSelectsEachFaceByDirection (39580 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR16Uint
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x0000000103ffabfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x0000000103ffbf74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x0000000103ff557c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x0000000103ffffb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x000000010286f64c (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR16Uint_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x00000001053bcd00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x000000010533b4e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x000000010533b394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x000000010533cfcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x000000010533f678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x0000000105356f50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x00000001053d43dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x0000000105356488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x00000001053562dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x000000010530e16c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x000000010530e118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7172==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016d61e160 sp 0x00016d61dfd0 T51654)
==7172==Hint: pc points to the zero page.
==7172==The signal is caused by a READ memory access.
==7172==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x0001053bccfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x00010533b4e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x00010533b390 in testing::Test::Run() gtest.cc:2688
    #4 0x00010533cfc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x00010533f674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x000105356f4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x0001053d43d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x000105356484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x0001053562d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x00010530e168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x00010530e114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7172==Register values:
 x[0] = 0x000000016d61e0f8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017bf3df004ff000  x[13] = 0x017be3db804fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d00040600c000  x[17] = 0x00000007be3dbc00  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016d61ef68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016d61f0d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016d61e160     lr = 0x000000010286f64c     sp = 0x000000016d61dfd0
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7172==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-15-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/3/22 (13 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/3/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-3-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=3 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 4 of 22.
[==========] Running 8 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 2 tests from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadOutOfBoundsIndexReadsZeroWithoutTouchingHeap
[       OK ] RuntimeCPUTest.TypedLoadOutOfBoundsIndexReadsZeroWithoutTouchingHeap (35918 ms)
[ RUN      ] RuntimeCPUTest.RawLoadV4F32StructuredKindIsAccepted
[       OK ] RuntimeCPUTest.RawLoadV4F32StructuredKindIsAccepted (37672 ms)
[----------] 2 tests from RuntimeCPUTest (73591 ms total)

[----------] 6 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.LodBiasShiftsSelectedLevel
[       OK ] ImageSamplingTest.LodBiasShiftsSelectedLevel (37975 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR16G16Unorm
[       OK ] ImageSamplingTest.LoadFetchesR16G16Unorm (37930 ms)
[ RUN      ] ImageSamplingTest.LoadI32FetchesR32G32Uint
[       OK ] ImageSamplingTest.LoadI32FetchesR32G32Uint (39509 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR16G16B16A16SnormQuantized
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x00000001024eebfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x00000001024eff74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x00000001024e957c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x00000001024f3fb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x0000000100d5ce14 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR16G16B16A16SnormQuantized_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x00000001038b0d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x000000010382f4e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x000000010382f394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000103830fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x0000000103833678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x000000010384af50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x00000001038c83dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x000000010384a488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x000000010384a2dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x000000010380216c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x0000000103802118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7166==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016f12a160 sp 0x00016f129ea0 T51643)
==7166==Hint: pc points to the zero page.
==7166==The signal is caused by a READ memory access.
==7166==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x0001038b0cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x00010382f4e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x00010382f390 in testing::Test::Run() gtest.cc:2688
    #4 0x000103830fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x000103833674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x00010384af4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x0001038c83d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x00010384a484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x00010384a2d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x000103802168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x000103802114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7166==Register values:
 x[0] = 0x000000016f12a0f0   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017bf3df004ff000  x[13] = 0x017be3db804fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d00065e00c000  x[17] = 0x00000007be3dbc00  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016f12af68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016f12b0d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016f12a160     lr = 0x0000000100d5ce14     sp = 0x000000016f129ea0
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7166==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-3-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/12/22 (14 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/12/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-12-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=12 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 13 of 22.
[==========] Running 8 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedStoreV4I32DroppedWithoutUavFlag
[       OK ] RuntimeCPUTest.TypedStoreV4I32DroppedWithoutUavFlag (35487 ms)
[----------] 1 test from RuntimeCPUTest (35487 ms total)

[----------] 7 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.ExplicitLodMagnifyingUsesMagFilterNotMinFilter
[       OK ] ImageSamplingTest.ExplicitLodMagnifyingUsesMagFilterNotMinFilter (37849 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR11G11B10Float
[       OK ] ImageSamplingTest.LoadFetchesR11G11B10Float (37688 ms)
[ RUN      ] ImageSamplingTest.InactiveLaneReadsZero
[       OK ] ImageSamplingTest.InactiveLaneReadsZero (38557 ms)
[ RUN      ] ImageSamplingTest.Load2DArrayOutOfRangeLayerReadsZero
[       OK ] ImageSamplingTest.Load2DArrayOutOfRangeLayerReadsZero (39717 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR16FloatViaHalfEncode
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x0000000103ccebfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x0000000103ccff74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x0000000103cc957c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x0000000103cd3fb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x00000001025421f8 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR16FloatViaHalfEncode_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x0000000105090d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x000000010500f4e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x000000010500f394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000105010fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x0000000105013678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x000000010502af50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x00000001050a83dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x000000010502a488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x000000010502a2dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x0000000104fe216c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x0000000104fe2118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7176==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016d94a160 sp 0x00016d949fd0 T51663)
==7176==Hint: pc points to the zero page.
==7176==The signal is caused by a READ memory access.
==7176==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x000105090cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x00010500f4e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x00010500f390 in testing::Test::Run() gtest.cc:2688
    #4 0x000105010fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x000105013674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x00010502af4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x0001050a83d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x00010502a484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x00010502a2d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x000104fe2168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x000104fe2114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7176==Register values:
 x[0] = 0x000000016d94a0f8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017be3de804ff000  x[13] = 0x017bd3db004fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d00045a00c000  x[17] = 0x00000007bd3db400  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016d94af68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016d94b0d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016d94a160     lr = 0x00000001025421f8     sp = 0x000000016d949fd0
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7176==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-12-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/4/22 (15 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/4/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-4-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=4 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 5 of 22.
[==========] Running 8 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 2 tests from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadInactiveMaskReadsZero
[       OK ] RuntimeCPUTest.TypedLoadInactiveMaskReadsZero (35710 ms)
[ RUN      ] RuntimeCPUTest.RawLoadStoreRoundTripV2F32
[       OK ] RuntimeCPUTest.RawLoadStoreRoundTripV2F32 (37565 ms)
[----------] 2 tests from RuntimeCPUTest (73275 ms total)

[----------] 6 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.AnisotropicSampleDiffersFromIsotropicSample
[       OK ] ImageSamplingTest.AnisotropicSampleDiffersFromIsotropicSample (37675 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR16G16Snorm
[       OK ] ImageSamplingTest.LoadFetchesR16G16Snorm (38138 ms)
[ RUN      ] ImageSamplingTest.LoadI32FetchesR32G32Sint
[       OK ] ImageSamplingTest.LoadI32FetchesR32G32Sint (39430 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR8UnormQuantized
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x0000000101a3abfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x0000000101a3bf74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x0000000101a3557c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x0000000101a3ffb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x00000001002a99e8 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR8UnormQuantized_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x0000000102dfcd00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x0000000102d7b4e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x0000000102d7b394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000102d7cfcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x0000000102d7f678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x0000000102d96f50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x0000000102e143dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x0000000102d96488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x0000000102d962dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x0000000102d4e16c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x0000000102d4e118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7177==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016fbde160 sp 0x00016fbddfd0 T51665)
==7177==Hint: pc points to the zero page.
==7177==The signal is caused by a READ memory access.
==7177==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x000102dfccfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x000102d7b4e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x000102d7b390 in testing::Test::Run() gtest.cc:2688
    #4 0x000102d7cfc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x000102d7f674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x000102d96f4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x000102e143d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x000102d96484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x000102d962d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x000102d4e168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x000102d4e114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7177==Register values:
 x[0] = 0x000000016fbde0f8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017c13e0004ff000  x[13] = 0x017c03dc804fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d00053000c000  x[17] = 0x00000007c03dcc00  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016fbdef68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016fbdf0d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016fbde160     lr = 0x00000001002a99e8     sp = 0x000000016fbddfd0
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7177==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-4-22.json
********************
FAIL: FeMe-Unit :: Import/SPIRV/./FeMeImportSPIRVTests/2/4 (16 of 249)
******************** TEST 'FeMe-Unit :: Import/SPIRV/./FeMeImportSPIRVTests/2/4' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Import/SPIRV/./FeMeImportSPIRVTests-FeMe-Unit-7115-2-4.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=4 GTEST_SHARD_INDEX=2 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Import/SPIRV/./FeMeImportSPIRVTests
--

Note: This is test shard 3 of 4.
[==========] Running 1 test from 1 test suite.
[----------] Global test environment set-up.
[----------] 1 test from SPIRVImporterTest
[ RUN      ] SPIRVImporterTest.RejectsMalformedBinary
/Users/cbieneman/dev/llvm-project/llvm/include/llvm/ADT/SmallVector.h:524:53: runtime error: load of misaligned address 0x000107ee03a7 for type 'const unsigned int *', which requires 4 byte alignment
0x000107ee03a7: note: pointer points here
 74 2e 68 00 de  ad be ef 00 4e 6f 6e 53  65 6d 61 6e 74 69 63 2e  44 65 62 75 67 50 72 69  6e 74 66
             ^
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior /Users/cbieneman/dev/llvm-project/llvm/include/llvm/ADT/SmallVector.h:524:53
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeImportSPIRVTests                0x0000000104dee3d0 llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeImportSPIRVTests                0x0000000104def878 PrintStackTraceSignalHandler(void*) + 112
2  FeMeImportSPIRVTests                0x0000000104de8598 llvm::sys::RunSignalHandlers() + 524
3  FeMeImportSPIRVTests                0x0000000104df536c SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib            0x0000000189f73744 _sigtramp + 56
5  libsystem_pthread.dylib             0x0000000189f698d8 pthread_kill + 296
6  libsystem_c.dylib                   0x0000000189e70644 abort + 148
7  libclang_rt.ubsan_osx_dynamic.dylib 0x000000010f548298 __sanitizer::Atexit(void (*)()) + 0
8  libclang_rt.ubsan_osx_dynamic.dylib 0x000000010f547890 __sanitizer::Die() + 108
9  libclang_rt.ubsan_osx_dynamic.dylib 0x000000010f52b6c0 __ubsan_handle_alignment_assumption + 0
10 FeMeImportSPIRVTests                0x000000010494f42c void llvm::SmallVectorTemplateBase<unsigned int, true>::uninitialized_copy<unsigned int const*, unsigned int*>(unsigned int const*, unsigned int const*, unsigned int*) + 116
11 FeMeImportSPIRVTests                0x000000010494efc4 void llvm::SmallVectorImpl<unsigned int>::append<unsigned int const*, void>(unsigned int const*, unsigned int const*) + 272
12 FeMeImportSPIRVTests                0x0000000104f2f670 llvm::SmallVector<unsigned int, 12u>::SmallVector<unsigned int, void>(llvm::ArrayRef<unsigned int>) + 212
13 FeMeImportSPIRVTests                0x0000000104f2edf8 llvm::SmallVector<unsigned int, 12u>::SmallVector<unsigned int, void>(llvm::ArrayRef<unsigned int>) + 92
14 FeMeImportSPIRVTests                0x0000000104f2c83c (anonymous namespace)::stripNonSemanticExtInst(llvm::ArrayRef<unsigned int>) + 152
15 FeMeImportSPIRVTests                0x0000000104f2c31c feme::SPIRVImporter::import(llvm::MemoryBufferRef, feme::ImportOptions const&, feme::Context&) const + 692
16 FeMeImportSPIRVTests                0x000000010494c950 (anonymous namespace)::SPIRVImporterTest_RejectsMalformedBinary_Test::TestBody() + 216
17 FeMeImportSPIRVTests                0x0000000104eba77c void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
18 FeMeImportSPIRVTests                0x0000000104e331d4 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
19 FeMeImportSPIRVTests                0x0000000104e33080 testing::Test::Run() + 460
20 FeMeImportSPIRVTests                0x0000000104e34cb8 testing::TestInfo::Run() + 1072
21 FeMeImportSPIRVTests                0x0000000104e37364 testing::TestSuite::Run() + 1368
22 FeMeImportSPIRVTests                0x0000000104e4ee30 testing::internal::UnitTestImpl::RunAllTests() + 2584
23 FeMeImportSPIRVTests                0x0000000104ed1e58 bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
24 FeMeImportSPIRVTests                0x0000000104e4e368 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
25 FeMeImportSPIRVTests                0x0000000104e4e1bc testing::UnitTest::Run() + 348
26 FeMeImportSPIRVTests                0x0000000104e047e0 RUN_ALL_TESTS() + 72
27 FeMeImportSPIRVTests                0x0000000104e0478c main + 308
28 dyld                                0x0000000189babe00 start + 6992

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Import/SPIRV/./FeMeImportSPIRVTests-FeMe-Unit-7115-2-4.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/2/22 (17 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/2/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-2-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=2 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 3 of 22.
[==========] Running 8 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 2 tests from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadPackedR8G8B8A8Snorm
[       OK ] RuntimeCPUTest.TypedLoadPackedR8G8B8A8Snorm (35873 ms)
[ RUN      ] RuntimeCPUTest.RawStoreV4F32DroppedWithoutUavFlag
[       OK ] RuntimeCPUTest.RawStoreV4F32DroppedWithoutUavFlag (37845 ms)
[----------] 2 tests from RuntimeCPUTest (73719 ms total)

[----------] 6 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.MinLodClampsExplicitSampleAboveBaseLevel
[       OK ] ImageSamplingTest.MinLodClampsExplicitSampleAboveBaseLevel (37980 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR16G16Float
[       OK ] ImageSamplingTest.LoadFetchesR16G16Float (38068 ms)
[ RUN      ] ImageSamplingTest.LoadI32FetchesR16G16Sint
[       OK ] ImageSamplingTest.LoadI32FetchesR16G16Sint (39689 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR16G16B16A16UnormQuantized
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x00000001059f6bfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x00000001059f7f74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x00000001059f157c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x00000001059fbfb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x0000000104264234 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR16G16B16A16UnormQuantized_Test::TestBody() + 292
6  FeMeRuntimeCPUTests      0x0000000106db8d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x0000000106d374e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x0000000106d37394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000106d38fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x0000000106d3b678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x0000000106d52f50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x0000000106dd03dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x0000000106d52488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x0000000106d522dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x0000000106d0a16c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x0000000106d0a118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7167==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016bc22160 sp 0x00016bc21ea0 T51645)
==7167==Hint: pc points to the zero page.
==7167==The signal is caused by a READ memory access.
==7167==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x000106db8cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x000106d374e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x000106d37390 in testing::Test::Run() gtest.cc:2688
    #4 0x000106d38fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x000106d3b674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x000106d52f4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x000106dd03d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x000106d52484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x000106d522d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x000106d0a168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x000106d0a114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7167==Register values:
 x[0] = 0x000000016bc220f0   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017bf3df004ff000  x[13] = 0x017be3db804fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d00056400c000  x[17] = 0x00000007be3dbc00  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016bc22f68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016bc230d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016bc22160     lr = 0x0000000104264234     sp = 0x000000016bc21ea0
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7167==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-2-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/18/22 (244 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/18/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-18-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=18 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 19 of 22.
[==========] Running 7 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TrustedFlagSkipsOffsetCheck
[       OK ] RuntimeCPUTest.TrustedFlagSkipsOffsetCheck (42830 ms)
[----------] 1 test from RuntimeCPUTest (42830 ms total)

[----------] 6 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.ClampToBorderReadsBorderColor
[       OK ] ImageSamplingTest.ClampToBorderReadsBorderColor (42767 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR8Snorm
[       OK ] ImageSamplingTest.LoadFetchesR8Snorm (31748 ms)
[ RUN      ] ImageSamplingTest.LoadI32FetchesR8Sint
[       OK ] ImageSamplingTest.LoadI32FetchesR8Sint (27972 ms)
[ RUN      ] ImageSamplingTest.StoreWritesOnlyRedComponentIntoR32Float
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x00000001023f6bfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x00000001023f7f74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x00000001023f157c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x00000001023fbfb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x0000000100c5f094 (anonymous namespace)::ImageSamplingTest_StoreWritesOnlyRedComponentIntoR32Float_Test::TestBody() + 288
6  FeMeRuntimeCPUTests      0x00000001037b8d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x00000001037374e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x0000000103737394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000103738fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x000000010373b678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x0000000103752f50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x00000001037d03dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x0000000103752488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x00000001037522dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x000000010370a16c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x000000010370a118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7255==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016f222160 sp 0x00016f221fe0 T54348)
==7255==Hint: pc points to the zero page.
==7255==The signal is caused by a READ memory access.
==7255==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x0001037b8cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x0001037374e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x000103737390 in testing::Test::Run() gtest.cc:2688
    #4 0x000103738fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x00010373b674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x000103752f4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x0001037d03d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x000103752484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x0001037522d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x00010370a168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x00010370a114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7255==Register values:
 x[0] = 0x000000016f2220f8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x01314189804ff000  x[13] = 0x01313186004fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d00062a00c000  x[17] = 0x0000000313186400  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016f222f68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016f2230d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016f222160     lr = 0x0000000100c5f094     sp = 0x000000016f221fe0
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7255==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-18-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/21/22 (245 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/21/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-21-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=21 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 22 of 22.
[==========] Running 7 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.RawLoadV4F32IdentityFormat
[       OK ] RuntimeCPUTest.RawLoadV4F32IdentityFormat (42657 ms)
[----------] 1 test from RuntimeCPUTest (42657 ms total)

[----------] 6 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.ImplicitLodWithNoDerivativesReadsBaseLevel
[       OK ] ImageSamplingTest.ImplicitLodWithNoDerivativesReadsBaseLevel (43174 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR16Float
[       OK ] ImageSamplingTest.LoadFetchesR16Float (31549 ms)
[ RUN      ] ImageSamplingTest.LoadI32FetchesR16Uint
[       OK ] ImageSamplingTest.LoadI32FetchesR16Uint (27973 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR16G16B16A16FloatViaHalfEncode
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x0000000105b26bfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x0000000105b27f74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x0000000105b2157c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x0000000105b2bfb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x0000000104390cb4 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR16G16B16A16FloatViaHalfEncode_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x0000000106ee8d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x0000000106e674e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x0000000106e67394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000106e68fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x0000000106e6b678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x0000000106e82f50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x0000000106f003dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x0000000106e82488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x0000000106e822dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x0000000106e3a16c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x0000000106e3a118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7258==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016baf2160 sp 0x00016baf1ea0 T54365)
==7258==Hint: pc points to the zero page.
==7258==The signal is caused by a READ memory access.
==7258==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x000106ee8cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x000106e674e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x000106e67390 in testing::Test::Run() gtest.cc:2688
    #4 0x000106e68fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x000106e6b674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x000106e82f4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x000106f003d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x000106e82484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x000106e822d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x000106e3a168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x000106e3a114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7258==Register values:
 x[0] = 0x000000016baf20f0   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x01313189004ff000  x[13] = 0x01312185804fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d000c5c00c000  x[17] = 0x0000000312185c00  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016baf2f68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016baf30d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016baf2160     lr = 0x0000000104390cb4     sp = 0x000000016baf1ea0
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7258==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-21-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/19/22 (246 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/19/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-19-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=19 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 20 of 22.
[==========] Running 7 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.RawLoadStoreRoundTrip
[       OK ] RuntimeCPUTest.RawLoadStoreRoundTrip (42812 ms)
[----------] 1 test from RuntimeCPUTest (42812 ms total)

[----------] 6 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.SRGBDecodeOnSample
[       OK ] ImageSamplingTest.SRGBDecodeOnSample (43259 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR8G8Unorm
[       OK ] ImageSamplingTest.LoadFetchesR8G8Unorm (31527 ms)
[ RUN      ] ImageSamplingTest.LoadI32FetchesR8G8Uint
[       OK ] ImageSamplingTest.LoadI32FetchesR8G8Uint (27956 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR32G32B32A32Uint
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x0000000101ad6bfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x0000000101ad7f74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x0000000101ad157c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x0000000101adbfb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x000000010033f758 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR32G32B32A32Uint_Test::TestBody() + 284
6  FeMeRuntimeCPUTests      0x0000000102e98d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x0000000102e174e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x0000000102e17394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000102e18fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x0000000102e1b678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x0000000102e32f50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x0000000102eb03dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x0000000102e32488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x0000000102e322dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x0000000102dea16c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x0000000102dea118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7256==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016fb42160 sp 0x00016fb41e80 T54355)
==7256==Hint: pc points to the zero page.
==7256==The signal is caused by a READ memory access.
==7256==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x000102e98cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x000102e174e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x000102e17390 in testing::Test::Run() gtest.cc:2688
    #4 0x000102e18fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x000102e1b674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x000102e32f4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x000102eb03d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x000102e32484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x000102e322d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x000102dea168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x000102dea114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7256==Register values:
 x[0] = 0x000000016fb420e8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x01314189804ff000  x[13] = 0x01313186004fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d000be200c000  x[17] = 0x0000000313186400  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016fb42f68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016fb430d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016fb42160     lr = 0x000000010033f758     sp = 0x000000016fb41e80
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7256==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-19-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/17/22 (247 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/17/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-17-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=17 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 18 of 22.
[==========] Running 7 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadOutOfRangeOffsetReadsZero
[       OK ] RuntimeCPUTest.TypedLoadOutOfRangeOffsetReadsZero (43165 ms)
[----------] 1 test from RuntimeCPUTest (43165 ms total)

[----------] 6 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.RepeatAddressingWrapsCoordinate
[       OK ] ImageSamplingTest.RepeatAddressingWrapsCoordinate (43131 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR8Unorm
[       OK ] ImageSamplingTest.LoadFetchesR8Unorm (31573 ms)
[ RUN      ] ImageSamplingTest.LoadI32FetchesR8Uint
[       OK ] ImageSamplingTest.LoadI32FetchesR8Uint (27933 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR32G32B32A32Float
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x00000001020f6bfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x00000001020f7f74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x00000001020f157c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x00000001020fbfb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x000000010095dca0 (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR32G32B32A32Float_Test::TestBody() + 300
6  FeMeRuntimeCPUTests      0x00000001034b8d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x00000001034374e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x0000000103437394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000103438fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x000000010343b678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x0000000103452f50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x00000001034d03dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x0000000103452488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x00000001034522dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x000000010340a16c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x000000010340a118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7253==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016f522160 sp 0x00016f521d20 T54343)
==7253==Hint: pc points to the zero page.
==7253==The signal is caused by a READ memory access.
==7253==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x0001034b8cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x0001034374e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x000103437390 in testing::Test::Run() gtest.cc:2688
    #4 0x000103438fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x00010343b674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x000103452f4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x0001034d03d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x000103452484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x0001034522d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x00010340a168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x00010340a114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7253==Register values:
 x[0] = 0x000000016f5220b8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000001
 x[4] = 0x0000000000000001   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x01313189004ff000  x[13] = 0x01312185804fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d0005c000c000  x[17] = 0x0000000312185c00  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016f522f68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016f5230d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016f522160     lr = 0x000000010095dca0     sp = 0x000000016f521d20
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7253==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-17-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/20/22 (248 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/20/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-20-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=20 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 21 of 22.
[==========] Running 7 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.StructuredBufferKindIsAccepted
[       OK ] RuntimeCPUTest.StructuredBufferKindIsAccepted (43119 ms)
[----------] 1 test from RuntimeCPUTest (43119 ms total)

[----------] 6 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.ExplicitLodSelectsMipLevel
[       OK ] ImageSamplingTest.ExplicitLodSelectsMipLevel (43647 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesR8G8Snorm
[       OK ] ImageSamplingTest.LoadFetchesR8G8Snorm (31472 ms)
[ RUN      ] ImageSamplingTest.LoadI32FetchesR8G8Sint
[       OK ] ImageSamplingTest.LoadI32FetchesR8G8Sint (27917 ms)
[ RUN      ] ImageSamplingTest.StoreWritesOnlyRedComponentIntoR32Sint
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x000000010471ebfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x000000010471ff74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x000000010471957c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x0000000104723fb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x0000000102f885f0 (anonymous namespace)::ImageSamplingTest_StoreWritesOnlyRedComponentIntoR32Sint_Test::TestBody() + 288
6  FeMeRuntimeCPUTests      0x0000000105ae0d00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x0000000105a5f4e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x0000000105a5f394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x0000000105a60fcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x0000000105a63678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x0000000105a7af50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x0000000105af83dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x0000000105a7a488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x0000000105a7a2dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x0000000105a3216c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x0000000105a32118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7259==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016cefa160 sp 0x00016cef9fd0 T54370)
==7259==Hint: pc points to the zero page.
==7259==The signal is caused by a READ memory access.
==7259==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x000105ae0cfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x000105a5f4e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x000105a5f390 in testing::Test::Run() gtest.cc:2688
    #4 0x000105a60fc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x000105a63674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x000105a7af4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x000105af83d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x000105a7a484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x000105a7a2d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x000105a32168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x000105a32114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7259==Register values:
 x[0] = 0x000000016cefa0f8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x01314189804ff000  x[13] = 0x01313186004fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d0005d000c000  x[17] = 0x0000000313186400  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016cefaf68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016cefb0d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016cefa160     lr = 0x0000000102f885f0     sp = 0x000000016cef9fd0
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7259==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-20-22.json
********************
FAIL: FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/9/22 (249 of 249)
******************** TEST 'FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/9/22' FAILED ********************
Script(shard):
--
GTEST_OUTPUT=json:/Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-9-22.json GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=22 GTEST_SHARD_INDEX=9 /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests
--

Note: This is test shard 10 of 22.
[==========] Running 8 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 1 test from RuntimeCPUTest
[ RUN      ] RuntimeCPUTest.TypedLoadV4I32IdentityFormat
[       OK ] RuntimeCPUTest.TypedLoadV4I32IdentityFormat (42914 ms)
[----------] 1 test from RuntimeCPUTest (42914 ms total)

[----------] 7 tests from ImageSamplingTest
[ RUN      ] ImageSamplingTest.PointSampleIdentityFormat
[       OK ] ImageSamplingTest.PointSampleIdentityFormat (42973 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesB8G8R8A8Unorm
[       OK ] ImageSamplingTest.LoadFetchesB8G8R8A8Unorm (31640 ms)
[ RUN      ] ImageSamplingTest.LoadFetchesS8Uint
[       OK ] ImageSamplingTest.LoadFetchesS8Uint (27936 ms)
[ RUN      ] ImageSamplingTest.Sample2DArrayReadsRequestedLayer
[       OK ] ImageSamplingTest.Sample2DArrayReadsRequestedLayer (24900 ms)
[ RUN      ] ImageSamplingTest.StoreWritesTexelIntoR8G8SnormQuantized
Stack dump without symbol names (ensure you have llvm-symbolizer in your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point to it):
0  FeMeRuntimeCPUTests      0x0000000105f0abfc llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) + 108
1  FeMeRuntimeCPUTests      0x0000000105f0bf74 PrintStackTraceSignalHandler(void*) + 112
2  FeMeRuntimeCPUTests      0x0000000105f0557c llvm::sys::RunSignalHandlers() + 524
3  FeMeRuntimeCPUTests      0x0000000105f0ffb0 SignalHandler(int, __siginfo*, void*) + 328
4  libsystem_platform.dylib 0x0000000189f73744 _sigtramp + 56
5  FeMeRuntimeCPUTests      0x000000010477c89c (anonymous namespace)::ImageSamplingTest_StoreWritesTexelIntoR8G8SnormQuantized_Test::TestBody() + 280
6  FeMeRuntimeCPUTests      0x00000001072ccd00 void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 176
7  FeMeRuntimeCPUTests      0x000000010724b4e8 void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) + 148
8  FeMeRuntimeCPUTests      0x000000010724b394 testing::Test::Run() + 460
9  FeMeRuntimeCPUTests      0x000000010724cfcc testing::TestInfo::Run() + 1072
10 FeMeRuntimeCPUTests      0x000000010724f678 testing::TestSuite::Run() + 1368
11 FeMeRuntimeCPUTests      0x0000000107266f50 testing::internal::UnitTestImpl::RunAllTests() + 2584
12 FeMeRuntimeCPUTests      0x00000001072e43dc bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 176
13 FeMeRuntimeCPUTests      0x0000000107266488 bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) + 148
14 FeMeRuntimeCPUTests      0x00000001072662dc testing::UnitTest::Run() + 348
15 FeMeRuntimeCPUTests      0x000000010721e16c RUN_ALL_TESTS() + 72
16 FeMeRuntimeCPUTests      0x000000010721e118 main + 308
17 dyld                     0x0000000189babe00 start + 6992
UndefinedBehaviorSanitizer:DEADLYSIGNAL
==7257==ERROR: UndefinedBehaviorSanitizer: SEGV on unknown address 0x000000000000 (pc 0x000000000000 bp 0x00016b70e160 sp 0x00016b70df80 T54358)
==7257==Hint: pc points to the zero page.
==7257==The signal is caused by a READ memory access.
==7257==Hint: address points to the zero page.
    #0 0x000000000000  (<unknown module>)
    #1 0x0001072cccfc in void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2613
    #2 0x00010724b4e4 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) gtest.cc:2668
    #3 0x00010724b390 in testing::Test::Run() gtest.cc:2688
    #4 0x00010724cfc8 in testing::TestInfo::Run() gtest.cc:2837
    #5 0x00010724f674 in testing::TestSuite::Run() gtest.cc:3016
    #6 0x000107266f4c in testing::internal::UnitTestImpl::RunAllTests() gtest.cc:5921
    #7 0x0001072e43d8 in bool testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2613
    #8 0x000107266484 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) gtest.cc:2668
    #9 0x0001072662d8 in testing::UnitTest::Run() gtest.cc:5485
    #10 0x00010721e168 in RUN_ALL_TESTS() gtest.h:2317
    #11 0x00010721e114 in main TestMain.cpp:55
    #12 0x000189babdfc in start+0x1b4c (dyld:arm64e+0x1fdfc)

==7257==Register values:
 x[0] = 0x000000016b70e0f8   x[1] = 0x0000000000000001   x[2] = 0x0000000000000000   x[3] = 0x0000000000000000
 x[4] = 0x0000000000000000   x[5] = 0x0000000000000001   x[6] = 0x0000000000000032   x[7] = 0xfffff0003ffff800
 x[8] = 0x0000000000000000   x[9] = 0x0000000000000001  x[10] = 0x0000000000000000  x[11] = 0x0000000000000000
x[12] = 0x017be3de804ff000  x[13] = 0x017bd3db004fec00  x[14] = 0x00000000000ff000  x[15] = 0x0000000000000007
x[16] = 0x952d00062a00c000  x[17] = 0x00000007bd3db400  x[18] = 0x0000000000000000  x[19] = 0x00000001f5ebc058
x[20] = 0x00000001f5ebc100  x[21] = 0x000000016b70ef68  x[22] = 0xfffffffffffffff0  x[23] = 0x00000001f6173660
x[24] = 0x0000000000000001  x[25] = 0x000000016b70f0d0  x[26] = 0x00000001f6173670  x[27] = 0x0000000000000000
x[28] = 0x0000000000000000     fp = 0x000000016b70e160     lr = 0x000000010477c89c     sp = 0x000000016b70df80
UndefinedBehaviorSanitizer can not provide additional info.
SUMMARY: UndefinedBehaviorSanitizer: SEGV (<unknown module>)
==7257==ABORTING

--
exit: -6
--
shard JSON output does not exist: /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/unittests/Runtime/CPU/./FeMeRuntimeCPUTests-FeMe-Unit-7115-9-22.json
********************
********************
Failed Tests (23):
  FeMe-Unit :: Import/SPIRV/./FeMeImportSPIRVTests/2/4
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/0/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/1/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/10/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/11/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/12/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/13/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/14/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/15/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/16/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/17/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/18/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/19/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/2/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/20/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/21/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/3/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/4/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/5/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/6/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/7/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/8/22
  FeMe-Unit :: Runtime/CPU/./FeMeRuntimeCPUTests/9/22


Testing Time: 369.58s

Total Discovered Tests: 1359
  Passed: 1336 (98.31%)
  Failed:   23 (1.69%)
FAILED: tools/feme/test/CMakeFiles/check-feme-unit /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/test/CMakeFiles/check-feme-unit
cd /Users/cbieneman/dev/llvm-project/build-dbg/tools/feme/test && /Applications/Xcode.app/Contents/Developer/Library/Frameworks/Python3.framework/Versions/3.9/bin/python3.9 /Users/cbieneman/dev/llvm-project/build-dbg/./bin/llvm-lit -sv /Users/cbieneman/dev/llvm-project/feme/test/Unit
ninja: build stopped: subcommand failed.
```
