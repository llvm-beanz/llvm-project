---
model: claude-sonnet-5
resume: b8ce7a07-6a6f-4205-864b-b09621346aaf
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

**Always**: Make sure you are not using precompiled headers in your build.

# Request

Can you please continue the work on the Vulkan FeMe ICD? A previous session
suggested the next steps:

1. **(~1 day)** Finish root-causing `L419(j)`'s `output_location` pair:
   open `StageIOArrayAccessChainPattern` in `SPIRVToLLVMPatterns.cpp` and
   trace the exact GEP/extract sequence it emits for a dynamic row index
   into a `vec3`-element array followed by a `.x`/`CompositeExtract`, vs.
   the same for `vec2`/`vec3`-full (both of which work). Likely suspects:
   an index computed from the *output* component count being reused for
   the *input* read (a 3-vs-1 width mismatch somewhere in address/stride
   math), or a bitcast/GEP that assumes matching vector widths on both
   sides of the dynamic access chain. Once found: fix, add a unit test at
   the SPIR-V→LLVM lowering phase (narrowest-possible repro: a minimal
   module with a dynamically-indexed `vec3[N]` input array read via
   `.x`-only `CompositeExtract` into a scalar output array), rebuild, and
   confirm both `output_location` cases + the full 28-case group pass with
   0 regressions.
2. **(~2-4 hours)** `L419(j)`'s `line_continuity` pair (`line-strip`,
   `polygon-mode-lines`) — still untriaged, next signature after
   `output_location` closes.
3. **(~1-2 hours each)** `L419(j)`'s remaining 2 singletons:
   `primitive_id.matching` and `device_mandatory_features` — still
   untriaged.
4. **(standing)** Keep doc sync, offload-test-suite pattern check, and the
   `CMAKE_BUILD_TYPE` decision on the list for whenever they become
   relevant.
