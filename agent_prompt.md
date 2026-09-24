---
model: claude-sonnet-5
resume: 6e011932-a46a-43ae-97b3-283c96c999ff
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

Can you please work on the FeMe ICD implementation? The previous session gave
the next steps:

1. **(~1 day, well-scoped, next session should start here)** `L185`:
   `opcompositeinsert.struct16arr3` hits a genuine aggregate-typed `phi`
   reaching `feme-cpu-simdize`, violating that pass's own documented
   invariant ("`LinearizePass` always rewrites one into a `select`
   before this pass ever runs"). Root cause is very likely in
   `feme/lib/Transforms/CPU/Linearize.cpp`, not `SIMDize.cpp` -- don't
   hack around it in `SIMDize.cpp`. Get the case's SPIR-V via its QPA
   log (same `feme-translate --import-spirv`/`--spirv-to-llvmir` pipe
   `L183`/`L184` both used), find the exact CFG shape not yet rewritten
   (likely a loop-carried or multi-predecessor aggregate `phi`), fix in
   `Linearize.cpp`.
2. **(2-4 hrs, one-time setup, still not done across several sessions)**
   `offload-test-suite`'s `check-hlsl-feme-vk` has no build directory at
   `/home/dev/dev/offload-test-suite/build` -- deferred again this
   session given the size of the L184 work. Needs a from-scratch build
   before it can run at all.
3. **(~5 min)** Scratch cleanup: `/tmp/ctsrun_l184/`, `/tmp/l184_*.mlir`,
   `/tmp/l184_frexp*`, `/tmp/patch_*.diff`, `/tmp/simdize_*.diff`,
   `/tmp/patterns_full.diff`, `/tmp/frexp_pattern.diff`,
   `/tmp/final_check.diff` -- none referenced by anything committed.
4. Scan `Roadmap.md` for the next open, well-scoped item once `L185` is
   picked up or skipped -- the last full-scan candidates from several
   sessions back (`L90`-`L95`, `L116`/`L116(b)`/`L116(d)`/`L116(f)`,
   `L126(a)`, `L147`, `L98(b)`, plus assorted `R`/`V`/`W`-prefixed rows)
   are still individually unvetted.
