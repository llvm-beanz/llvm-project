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

Can you work the the H-series milestones?

The last session suggested the next steps:

1. **Root-cause H112** (~45-90 min): the static IR is provably correct,
   so the bug is at runtime. Fastest path: add a hand-written
   `ExecutorTest.cpp`-level repro (mirroring H111(b)'s own successful
   bisection technique) driving the compiled hull+domain pipeline
   directly, and inspect the actual per-control-point `ClipDistance`
   values in `PatchPipeline.cpp`'s `copyLinkedElements` with a debugger or
   printf-instrumentation, comparing against the already-correct
   `gl_Position` handling in the exact same function.
2. Once H112 is fixed, re-run the full `dEQP-VK.clipping.user_defined.
   {clip_distance,clip_cull_distance}.*` matrix (non-`_dynamic_index`)
   to confirm 128/128, then flip `shaderClipDistance`/`shaderCullDistance`
   to `VK_TRUE` for real and close H53 and H32.
3. **Separately, not yet investigated**: the `_dynamic_index` variants
   combined with `vert_geom`/`vert_tess_geom` fail with `"JIT session
   error: Symbols not found: [ spirv_var_N ]"` -- a distinct bug, no
   milestone filed yet, worth a first diagnostic (~15-30 min) once H112
   closes.
4. `offload-test-suite`'s `check-hlsl-feme-vk` target is still never
   built/run in any session -- flagged again as a standing gap, not
   picked up this session either (out of scope for H32/H112, but worth a
   session of its own).
