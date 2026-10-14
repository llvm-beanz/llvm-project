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

1. **(~1-2 hours)** Grep-trace `scalarBlockLayout` next: check whether
   `SPIRVToLLVMPatterns.cpp`'s struct/block offset math already honors scalar
   (non-std140/std430) member offsets/strides as declared, or imposes its own
   alignment that would need relaxing.
   `VK_KHR_relaxed_block_layout`/`VK_KHR_uniform_buffer_standard_layout` are
   both already "core, not advertised by name" per
   `AdvertisedPromotedExtensions.txt` (declared offsets used as-is, no re-layout
   imposed) — if `scalarBlockLayout`'s stricter relaxation is already naturally
   covered by that same "use declared offsets as-is" policy, this could be
   another `K6d`-style free win. Verify empirically against real CTS cases
   before committing, the same way this session caught `variablePointers`.
2. **(~2-4 hours)** `VK_EXT_separate_stencil_usage`: `Image.cpp`'s `Image` class
   tracks one combined `Usage` field, not per-aspect — check whether any command
   actually validates usage flags strictly enough that a stencil-specific usage
   divergence would matter, or whether (like `VK_KHR_image_format_list`) the
   chained struct's absence from any validation path makes this also a pure
   no-op to accept.
3. **(~30 min)** `VK_KHR_shader_float_controls`: not traced at all yet this
   session or prior ones for actual feature-completeness (only known issue is an
   old manifest-entry drift blocking the full `Vulkan14FeatureInventory.md`
   regen).
4. **(standing)** `L419(j)` remaining untriaged items, `L418(a)` sanity
   re-check, backup tag decision, `CMAKE_BUILD_TYPE` decision.

**Immediate next action for next session:** grep-trace `scalarBlockLayout`'s
relationship to the already-implemented
`VK_KHR_relaxed_block_layout`/`VK_KHR_uniform_buffer_standard_layout` "no
re-layout imposed" policy in `SPIRVToLLVMPatterns.cpp`, then empirically verify
against a real CTS case list (not just absence-of-rejection) before committing
to it as `K6a`'s next split-off bit — same two-step discipline that caught
`variablePointers` this session.
