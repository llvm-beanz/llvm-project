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

1. **~1 day, needs a dedicated design session, the only remaining design-session
   item, carried over across many sessions now**: `L375` (`DiamondFlattener`
   stale-postdom-tree assertion, `switch_continue_{geom,tessc,tesse}` +
   `graphicsfuzz.nested-for-loops-switch-fallthrough`). This is now
   unambiguously the single biggest backlog item — every other queued triage
   item from the last several sessions has been resolved or newly scoped,
   leaving this as the clear next real bottleneck.
2. **~30-60 min**: `L390` (the 4 remaining
   `graphics_pipeline_library.local_read.*` ordinary Fails). Use
   `FEME_VULKAN_LOG_CREATION_ERRORS=1` on one reproducer first (the `L379(c)`
   playbook), falling back to `gdb bt` if the surfaced error isn't
   self-explanatory.
3. **~30 min, low priority, easy win, carried over across many sessions and
   still not done**: re-run the `renderpasses.dynamic_rendering.*` remaining
   ~828-case ordinary-Fail bucket's largest sub-families through one `gdb bt`
   each, the same approach that worked for
   `L379`/`L381`/`L382`/`L386`/`L387`/`L388`.
4. `LLVM_EXTERNAL_PROJECTS=OffloadTest` cache-persistence item: already
   resolved/documented, no action needed.
5. `array_of_matrices.test`'s XPASS: definitively closed this session (see
   above) — no further action needed, don't re-open.
