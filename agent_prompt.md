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

1. **(no deadline, opportunistic)** If a future session happens to be in
   `Sync.cpp`/`Image.cpp` for unrelated reasons, it's worth double-checking
   whether `vkQueueBindSparse`'s current "ignore the buffer/image bind arrays
   entirely" behavior needs revisiting once/if `H46` (real sparse resources)
   ever lands — right now that's correct by construction (no sparse resource can
   exist), but a future session enabling real sparse binding would need to go
   back and actually process `pBufferBinds`/`pImageOpaqueBinds`/`pImageBinds`
   instead of ignoring them.
2. **(~multi-session, real architecture work, separate from anything `L236(a)`
   touched)** `H46`: real sparse-resources feature work
   (`sparseBinding`/`sparseResidencyBuffer`/`sparseResidencyImage*` actually
   `VK_TRUE`), needing a genuine sparse-memory-binding allocator design. Not
   attempted this session, not expected to be a quick pickup.
3. **(ongoing, low priority, unchanged from last handoff)** Resume the broader
   `dEQP-VK.pipeline.*.blend.*` regression sweep for full 66-case-family `L411`
   coverage, if a future session wants it — start fresh with `detach: true`.
4. **(~1-2 hours, standing, no CTS-visible symptom found yet, unchanged from
   last handoff)** Keep doing periodic roadmap hygiene passes — this session's
   quick check found nothing, but it's worth repeating rather than assuming it
   stays clean forever.
