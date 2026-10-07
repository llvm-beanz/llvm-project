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
   item, carried over across many sessions now (genuinely not touched this
   session — deliberately deferred, consistent with explicit guidance carried
   over from the prior two sessions that two independent fix attempts already
   failed and a real design sitting is needed)**: `L375` (`DiamondFlattener`
   double-visitation of ordinary merge blocks by independent top-level
   `flatten()` calls). Do **not** re-attempt a PDT/DT recompute — confirmed via
   tracing (not guessing) that the predecessor count genuinely is 1 by the
   second visit, so recomputing cannot help. The next real step is a
   `FlattenedBlockMasks`-style memoization recording a block's final `Masks`
   once a walk fully reconverges past it, generalizing the existing
   `CycleBoundaryMasks`/H120 mechanism beyond cycle-boundary blocks to ordinary
   merge blocks.
2. **~30 min, low priority, carried over from last session, still not done**:
   the `primary_cmd_buff.random` bucket (756 Fails from the *old*,
   pre-`L390`/`L391` baseline — note this bucket's current Fail count needs
   re-confirming now that both `L390` and `L391` are fixed, since the full sweep
   above showed 0 Fail family-wide, meaning this bucket may already be fully
   resolved as a side effect and this item may simply be closed; check the full
   sweep's log for `random` case names before spending any time here, since the
   numbers may no longer apply).
3. **~1 hour, new this session**: investigate the 1,125-thread /
   brief-futex-stall anomaly observed during the
   `multisample_resolve.layers_3.*samples_8*` sweep (see the performance note
   above) — not a confirmed bug yet, just an anomaly worth a dedicated look with
   a narrower, isolated reproducer (a single `samples_8_baseLayer*` case run
   standalone, checked for thread count via `ps -eLf` before assuming it's
   actually a problem rather than expected CPU-renderer-under-heavy-MSAA-load
   behavior).
4. `LLVM_EXTERNAL_PROJECTS=OffloadTest` cache-persistence item: already
   resolved/documented, no action needed.
