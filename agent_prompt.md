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

1. **Implement L111(b)** (~2-4 hours -- new feature, not a one-line fix,
   since `Executor.cpp` has zero existing plumbing for a shader-written
   coverage mask). Read the fragment shader's own `SignatureSystemValue::
   Coverage` output once per invocation (same lookup pattern as the
   existing `FSAlphaToCoverage`), AND it together with the coverage mask
   already computed from rasterization/depth-stencil/alpha-to-coverage
   (never OR -- the shader's mask can only narrow coverage, per Vulkan's
   sample-mask-test semantics), and apply it before the per-sample
   color/depth write loop (~`Executor.cpp` line 3174-3390).
2. **Re-run `unusual_multisample_state` after L111(b) lands** -- expect
   it to flip to Pass, closing `pipeline_library.graphics_library.*`
   fully clean (548 Pass/0 Fail/287 NotSupported/1 pre-existing benign
   Warning).
3. **Then broaden the sweep (roadmap L106)** -- same untriaged candidates
   noted for several sessions running: a fresh `pipeline.*` subgroup
   (`pipeline.monolithic.*`, `pipeline.multisample.*`) or a top-level
   group outside `pipeline.*` (`subgroups.*`, `compute.*`,
   `graphicsfuzz.*`). ~30-60 minutes to pick the cheapest-looking one.
4. **Standing gotcha, still true**: export
   `VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json`
   before any `vulkaninfo`/`deqp-vk` in a fresh shell -- not persisted.
5. **Technique confirmed again this session**: when a prior session's
   own "confirmed distinct from X" note turns out to still be an
   assumption rather than a direct repro, re-run the case with `deqp-vk`
   directly before trusting it -- this session's own case was assumed to
   be a runtime image mismatch (like every other `misc.other.*` case)
   but was actually a pipeline-creation crash, a completely different
   category of bug with a completely different fix location.
6. **Technique confirmed again**: when a stage-based restriction breaks
   existing tests, check whether those tests are testing the shape
   *mechanically* (with a deliberately unrealistic stage attribute) --
   if so, prefer a narrower fix (e.g. excluding a specific `BuiltIn`)
   over a broader one, rather than "fixing" the tests to match a
   stricter restriction that isn't actually needed for correctness.
