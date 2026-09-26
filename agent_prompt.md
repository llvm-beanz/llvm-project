---
model: claude-sonnet-5
resume: ed0156d8-7270-4d71-9053-e6cff6d7f6b5
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file, and the environment-wide
agent skills at /home/dev/.agents/skills.

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
letter deep going forward (i.e. Q54(a)).

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done. Please
consult the i-have-adhd skill (from ~/.agents/skills) when writing the
agent_thoughts.md file. Please include suggested next steps if applicable in the
agent thoughts.

**Before doing anything else**: `vulkaninfo --summary | grep deviceName` and
confirm `FeMe CPU Vulkan Device`. Every session from now on, every time, not
just once at the start.

**When you find an issue outside FeMe**: Create an isolated reproducer, fix it,
and apply the fix in a commit that only touches files from outside the FeMe
subdirectory. Ensure that fixes to other LLVM sub-projects are self-contained
and tested.

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

# Request

Can you continue working on the FeMe ICD implementation? The previous session's
suggested next steps are:

## Suggested next steps (ranked, pick top one)

1. **L202(a)** (runtime gather-offset arithmetic in
   `SPIRVResourceLowering.cpp`): the highest-value remaining item since it's the
   only of the three original targets with a clear, scoped, and CTS-confirmed
   remaining blocker. Estimate: half a day to a full day, since it touches the
   CPU backend's actual sampling math, not just a dialect-conversion pattern.
2. **L201(a)** (`16bit_storage.input_output_*`, 300 cases, generic
   `VK_ERROR_INITIALIZATION_FAILED` with no diagnostic text): highest case-count
   remaining L201 cluster. Needs a `gdb` session or submission-path
   instrumentation to find the real error before any fix is possible. Estimate:
   1-2 hours just to get a real error message, unknown after that.
3. **L201(d)** (mesh-shader/tessellation f16 I/O correctness, 120 cases
   combined): "Result does not match reference" with no pixel-diff detail --
   needs `--deqp-log-images=enable` or a hand-built repro with known-good values
   to even start. Estimate: 1 hour to get first real diagnostic.
4. Cross-reference L201(e) (50 memory-model cases) against the existing
   milestone-9 barrier-linearization row before opening any new work -- it may
   already be a known, tracked duplicate. Estimate: 10 minutes.
5. Run `check-hlsl-feme-vk` from the `feme` branch of
   `/home/dev/dev/offload-test-suite` at least once, to validate this session's
   MatrixInverse/OuterProduct fixes against real `dxc`-compiled HLSL shapes, not
   just glslang-compiled CTS SPIR-V. Estimate: 15-30 minutes if the branch is
   already fetched and builds cleanly.
