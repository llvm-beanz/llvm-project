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

1. **(~5 min, do this first if picking up `L183`)** Re-read `L183`'s roadmap row
   before touching it -- it already has the 2 exact case names, the ruled-out
   hypotheses, and the "don't assume it's `L181`" warning. Don't re-derive any
   of this from scratch.
2. **(~1-2 hrs)** If picking up `L183`: get `feme-translate --import-spirv` ->
   `--spirv-to-llvmir` working as a 2-step pipe (confirmed working command
   shape: `feme-translate --import-spirv in.spv -o mid.mlir`, then
   `feme-translate --spirv-to-llvmir mid.mlir -o out.ll`) on a
   `FrexpStruct`+`half` compute shader, and diff its output against the same
   shader with a `ModfStruct` swap-in (which works) to find exactly where the
   two diverge.
3. **(~5 min)** `L98(b)` (`shaderFloat64` stage-IO) is still open and not
   attempted this session -- it needs genuinely variable-width storage or a
   two-slot decomposition, unlike `L98(a)`'s single-slot widen trick. See its
   own roadmap row for the design note.
4. Scan `Roadmap.md` for the next open, well-scoped item if not picking up
   `L183`/`L98(b)` -- the last full-scan candidates (`L90`-`L95`,
   `L116`/`L116(b)`/`L116(d)`/`L116(f)`, `L126(a)`, `L147`, plus
   `R`/`V`/`W`-prefixed rows) are still individually unvetted.
5. No `/tmp` scratch left from this session -- already cleaned up.
