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

1. **(~multi-session, still not attempted)** Design the per-tile
   `pImageBinds` model (`VkSparseImageMemoryBind`'s subresource/coordinate
   addressing) — explicitly scoped out of this and every prior session,
   needs its own design pass before any code.
2. **(~multi-session, separate design track)** `vkGetBufferDeviceAddress`'s
   virtual-address-space-reservation design (mmap/mprotect-based), per this
   session's deferred-design writeup in `FeMeVulkanDesign.md` — needed before
   this one remaining Buffer-typed call site can become range-aware.
3. **(~2-4 hours, new, separate from the Buffer-side work just finished)**
   The Image-typed call sites from the 26-list (`Swapchain.cpp`, `Image.cpp`
   `texelPointer`/`blockPointer`, `ImageOps.cpp`'s image half,
   `RenderPass.cpp`, `CommandBuffer.cpp`'s sampled-image prep) still need
   their own migration pass — out of scope for this session, which covered
   Buffer-typed sites only. Image sub-range addressing (subresource/mip/
   layer) is more complex than a linear byte offset; expect this to need its
   own small design note before the mechanical migration, similar in spirit
   to the vertex/index work this session but with an extra dimension.
4. **(not started, separate from `H46`)** Only once items 1-3 above are done:
   flip `sparseBinding`/`sparseResidency*` feature bits to `VK_TRUE`. Do NOT
   do this prematurely.
5. **(~1-2 hours, standing, unchanged)** Keep doing periodic roadmap hygiene
   passes — this session's was a quick check, not a deep one; alternate a
   deeper pass back in soon per the standing cadence.
6. **(opportunistic, unchanged)** `L375`
   (`switch_continue_{geom,tessc,tesse}` `DiamondFlattener` assertion) still
   untouched, still needs its own dedicated design session.
7. **(opportunistic, general takeaway from this session)** When a
   mechanical-looking pointer-arithmetic migration changes what a
   pre-existing variable *means* (here: `Base` went from "buffer start" to
   "buffer start + Offset"), grep every use of that variable in the same
   function, not just the one line being edited — the bug this session hit
   was exactly this: one `Offset` subtraction was correctly dropped
   elsewhere in the same diff, but the indexing expression wasn't updated to
   match.
