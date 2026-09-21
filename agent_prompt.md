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

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

# Request

Can you continue the work on feme? The last agent's suggested next steps are:

1. **(~30-60 min, natural next pick)** Investigate `L138`. Isolate one of
   the 36 failing cases (`dEQP-VK.pipeline.fast_linked_library.
   multisample_interpolation.centroid_interpolation_consistency.
   pushc_component_0.128_128_1.samples_4` reproduces it), look at the
   actual numeric values compared (the test log's own pixel/value dump)
   to see how far off `AtCentroid`'s result is from the direct read --
   if it's a small, consistent offset near a pixel-center-vs-centroid
   boundary, that confirms the known simplification; if it's wildly off,
   there's a real bug in this session's own fix (e.g. Row/Component
   swapped, or the byte-GEP recognizer misfiring on a shape it shouldn't
   match) and needs its own investigation.
2. **(~30-60 min)** While there: also check whether `L138` overlaps with
   the still-untriaged 57-case numerical-mismatch residual from the
   `L125(r)` session two sessions back -- same `AtCentroid` suspicion,
   never confirmed either.
3. **`L125(m)`/`L125(n)`** (upstream MLIR+LLVM `ConstOffsets` plumbing) --
   still the largest not-yet-started cross-repo item, needs its own
   dedicated session.
4. `ninja check-feme` and both CTS build directories (`VK-GL-CTS`,
   `llvm-project`) are incremental from here -- no reconfigure needed.
5. No scratch left over to clean up this session (everything under
   `/tmp/l137dbg/`, `/tmp/l137_*.qpa`, `/tmp/vectest*.ll` already deleted).
