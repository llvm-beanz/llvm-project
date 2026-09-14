---
model: claude-sonnet-5
resume: 3e3ed1ca-e8e0-43ee-a165-5cdf3bba2524
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file.

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

# Request

Can you work on the H-series milestones?

The previous session suggested the next steps:

1. **Triage the remaining 19 `mesh_shader.ext.*` failures** — the
   `synchronization.*` bucket (12 cases, all barrier-related) is the
   largest single group and untouched so far. ~15-30 min each for a first
   diagnostic.
2. **Audit other `widen*` helpers in `SIMDize.cpp` for the same bug
   pattern** — this session's fix was applied generically at the
   `ToErase` cleanup-loop level (so it covers every unconditional-widen
   producer, not just masked loads/stores), but a targeted audit of each
   individual `widen*` function for whether it *should* have gated on
   `isDivergentAtDef` earlier (skipping the widen outright, the way
   `widenVectorElementwise` does) rather than relying on this session's
   cleanup-loop safety net, hasn't been done. ~30 min grep + read.
3. **Find or construct a real CTS-level repro for H105** — still only
   proven by a hand-written unit test (unchanged from last session's
   suggestion, still not attempted). ~30-45 min to search/construct.
