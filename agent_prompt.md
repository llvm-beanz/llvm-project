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

1. **Implement L114(a)** (~2-4 hours -- an interpolation-architecture
   change, not a field addition). In `Executor.cpp`, recompute
   `Quad.Bary0`/`Bary1`/`Bary2` per `PassSample` using
   `(*SamplePositions)[PassSample]`'s own offset instead of the fixed
   pixel-center `Center`, and move (or duplicate) the varying-
   interpolation step so it runs per pass when `PerSampleShading` is
   true, not once before the pass loop. This closes the remaining 6
   `sample_position.correctness.*` failures.
2. **Also check the related, still-open sub-case noted but not
   confirmed**: a `Sample`-qualified varying with no
   `gl_SamplePosition`/`gl_SampleID` present does not force per-sample
   shading at all today -- worth a quick CTS grep for a case exercising
   exactly that shape once L114(a)'s main fix lands, to see if it needs
   its own follow-up or is already covered by the same fix.
3. **Re-sweep `multisample_shader_builtin.*` after L114(a) lands** --
   expect all 55 supported-sample-count cases to Pass (0 Fail), leaving
   only the 40 NotSupported cases -- still worth a spot-check of a
   couple of those to confirm they're a genuine capability gap
   (unsupported sample counts 16/32/64, per this session's own sweep
   output) rather than another undiscovered bug.
4. **Continue the L106 sweep** after L114(a) closes: same untriaged
   candidates noted for several sessions running --
   `multisample-interpolation.txt` (247 cases, small, also topically
   related, possibly *also* exposed by the same L114(a) interpolation
   gap -- worth picking this one next specifically because of that
   overlap) is the cheapest next pick; `pipeline.monolithic.*`/
   `subgroups.*`/`compute.*`/`graphicsfuzz.*` are much larger and still
   untriaged.
5. **Standing gotcha, still true**: export
   `VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json`
   before any `vulkaninfo`/`deqp-vk` in a fresh shell -- not persisted.
6. **Technique confirmed this session, worth repeating**: when a fix
   for a specific numeric constant (an enum value, a builtin ID, a
   decoration code, ...) comes from a prior session's own doc comment
   or notes rather than the authoritative spec/tablegen source, and the
   fix doesn't work on the first try, re-verify that constant directly
   against the source of truth (here, `mlir/include/mlir/Dialect/SPIRV/
   IR/SPIRVBase.td`) before re-reading application logic for bugs --
   the constant itself was wrong, not the logic around it, and this was
   the fastest possible way to find that out.
7. **Technique confirmed again**: after any fix that touches a shared,
   widely-used per-invocation ABI struct (`FemeFragmentInvocation`) or a
   condition gating a widely-shared code path (`PerSampleShading`), a
   CTS regression sweep of an unrelated-but-heavily-overlapping group
   (`pipeline_library.graphics_library.*`, 836 cases) is cheap insurance
   against silent collateral damage -- confirmed clean this session, but
   worth doing every time such a shared structure changes.
