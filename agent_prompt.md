---
model: claude-sonnet-5
resume: 6e011932-a46a-43ae-97b3-283c96c999ff
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

Can you please work on the FeMe ICD implementation? The previous session gave
the next steps:

1. **(~1-2 days, dedicated session)** Fix the two diagnosed-but-not-landed
   upstream bugs properly, as their own standalone contributions: (a)
   `SPIRVEmitIntrinsics.cpp`'s `preprocessCompositeConstants` hardcoded `i32`
   result type for `ConstantArray`/`ConstantStruct`/`ConstantDataArray` (should
   use `COp->getType()`, matching the `ConstantVector` branch); (b)
   `IRTranslator.cpp`'s generic intrinsic-call lowering path needs
   aggregate-operand splitting support (a much bigger lift -- would need its own
   design, likely mirroring `CallLowering`'s existing per-argument splitting
   machinery). Neither blocks anything currently, so this is
   optional/lower-priority, but both are real bugs that will bite the next
   person who tries to pass an array/struct value to any SPIR-V target
   intrinsic.
2. **`L125(n)`** is still the standing next real-Vulkan-correctness item per the
   last several sessions' logs (fix `isSupportedOffset` in
   `SPIRVResourceLowering.cpp` to reject rather than silently truncate a
   `4N`-wide flattened offset, then add the real `femeCpuImageGather*Offsets`
   runtime entry points) -- this session's `L125(p)` work is independent of it
   (different compile direction: `L125(p)` is LLVM-IR-to-SPIR-V for `dxc`,
   `L125(n)` is SPIR-V-to-CPU-runtime for feme's own import path) and does not
   unblock or change its scope.
3. **(~5 min)** `/tmp` scratch from this session (`l125p_repro.ll`,
   `l125p_repro2.ll`, `SPIRVEmitIntrinsics.cpp.bak`) already deleted. Two
   unrelated leftover files from earlier sessions (`check_feme_l125g.log`,
   `l125p_struct_repro.ll`, dated Sep 20/24) were left alone since they predate
   this session and aren't mine to judge as safe to delete.
