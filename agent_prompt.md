---
model: claude-sonnet-5
resume: db99d377-bdef-4dd8-a2b5-787bcf66bc23
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

1. **(~multi-session)** `L416` phase 2: switch `DeviceMemory`'s backing
   allocator to `memfd_create`+`mmap` on POSIX, preserving every existing
   `vkMapMemory`/`vkUnmapMemory`/flush/invalidate behavior exactly. This
   is the highest-risk remaining phase — touches every allocation in the
   driver. Needs a full `check-feme` + broad CTS memory/buffer/image
   regression sweep before landing, not just the targeted sanity check
   this session ran. Recommend starting with a design note on exactly
   which existing tests constitute "every device-memory CTS case" so the
   regression sweep has a concrete, enumerable target rather than an
   ad-hoc rerun.
2. **(~multi-session, unchanged)** Design the per-tile `pImageBinds` model
   (`VkSparseImageMemoryBind`'s subresource/coordinate addressing) — still
   untouched, still needs its own dedicated session; this session's `L416`
   design doesn't inform it much since buffers are linear and images are
   not.
3. **(blocked on 1-2)** Only once `L416` phases 2-4 and the `pImageBinds`
   design are done: flip `sparseBinding`/`sparseResidency*` feature bits
   to `VK_TRUE`.
4. **(done, no longer a handoff item)** `L375` — confirmed already fixed
   by a prior session (`af344681704c`). Stop carrying this forward.
5. **(low-priority, cosmetic)** The two roadmap hygiene findings above
   (`H124e(a)` nesting, `L66`/`F15`-style unsplit lettered sub-items) are
   recorded here but not fixed — only worth revisiting if a future
   cleanup session wants to spend real time rewriting closed historical
   rows for documentation clarity alone. Not a standing action item.
