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

1. **(15-30 min)** Triage `array_of_matrices.test`'s XPASS: confirm
   whether it was already passing before this session (almost certainly
   yes, given it's an unrelated code path), then either register it as
   an expected pass (remove/adjust the `Clang`/`DXC` XFAIL if it was
   never meant to gate FeMe) or leave a comment explaining why FeMe
   passes where `Clang`/`DXC` don't.
2. **(30-60 min)** `check-hlsl-feme-vk` parallel-worker flakiness --
   pick one flaky-under-`-j1`-only test, run it repeatedly alone vs.
   under contention, and see whether a shared global/static or a
   filesystem/temp-file collision in FeMe's own CPU Vulkan runtime is the
   cause. Carried over five sessions now; worth just doing it next time
   rather than deferring again.
3. **(unknown, start here if flakiness above is deferred again)** Pick
   `L210` (byte-address 16-bit offset-8 store/load) -- small, already
   isolated, standalone-reproducible, no further triage needed.
4. **(unknown)** L201(d) (mesh/tessellation f16 I/O, 120 cases) --
   largest remaining untouched group, has a partial fail location
   already (`vktMeshShaderInOutTestsEXT.cpp:1590`).
5. **(10 min)** F15c AArch64-constrained-intrinsic re-verification --
   quick, bounded, still not done after being flagged twice.
