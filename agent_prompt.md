---
model: claude-sonnet-5
resume: 52e661a0-b284-45ee-892f-3073721ca338
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

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

# Request

Can you continue the work on feme? The last agent's suggested next steps are:

1. **Find the exact pattern responsible for L103's wrong offset.** Add
   a temporary trace (`llvm::errs()` at pattern entry, or step through
   in `gdb`) while converting `dEQP-VK.pipeline.pipeline_library.
   spec_constant.graphics.fragment.composite.struct.ivec2`'s SPIR-V —
   confirm whether it's actually reaching upstream's generic
   `AccessChainPattern` (which should be correct, per the `opt` probe)
   or some other feme-specific fallback that computes offsets by hand.
   Rough estimate: 30–60 minutes, now that the exact wrong/right offset
   numbers (12 vs 16) and a standalone repro (`/tmp/l103_layout_test.ll`,
   recreatable with the snippet in `Roadmap.md`'s L103 entry) are known.
2. **Fix it** once found — likely either routing this shape through
   upstream's real type-indexed GEP (dropping whatever hand-computed
   byte-offset path currently wins), or fixing that path's own
   alignment arithmetic to match LLVM's `DataLayout`. Rough estimate:
   an hour, similar shape to L102's own fix once the responsible code
   is pinned down.
3. **Explain the `bvec*`-passes-but-`ivec*`-fails asymmetry** as part of
   the investigation — it's a real clue about which code path is
   involved (the two element types clearly go through different
   conversion logic somewhere).
4. **Re-sweep `composite.struct.*` and the full `spec_constant.*` group**
   after the fix, same "exact bucket-count shift, zero collateral
   regressions" validation used for L100/L101/L102.
5. **Standing gotcha, still true**: export
   `VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json`
   before any `vulkaninfo`/`deqp-vk` in a fresh shell.
