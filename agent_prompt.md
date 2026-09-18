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

1. **(~1-2 hours)** Stop hand-tracing IR for this specific case. Add real
   runtime instrumentation instead: a temporary host-callback intrinsic (check
   `feme/lib/Target/CPU/` for how existing `feme.cpu.resource.load.raw.*`-style
   calls are lowered to real function calls at codegen, and add a
   `feme.cpu.debug.print.i32`-shaped one following that exact pattern) that
   prints a value + lane index at runtime, inserted right before the specific
   `.uniform`-suffixed `extractelement` this row's original investigation
   flagged (block 52 of the `FEME_DUMP_IR=1` dump, or wherever it lands now) for
   the *actual* failing shader. This replaces guesswork with ground truth in one
   iteration instead of more manual algebra.
2. **(~30 min, if #1 doesn't immediately reveal it)** Since the fragment shader
   here has no derivatives, its `EntryMask` should be a simple "in-bounds pixel"
   mask, closer to the compute-style guarantee than the quad/helper-invocation
   case -- meaning lane 0 SHOULD behave safely per this session's own fix's own
   reasoning. If runtime instrumentation confirms lane 0's `EntryMask` bit
   really is always 1 for this shader, the bug is NOT in the recovery's lane
   choice at all, and is more likely in `widenMaskedAllocaStore`'s "run
   unconditionally, once per lane" model itself corrupting some *other* lane's
   (not lane 0's) storage in a way that later surfaces through a different,
   not-yet-identified path (e.g., a per-lane store into a `MaskedAllocas` array
   skipping a write for a discard-narrowed lane, then a later per-lane,
   unconditional -- not masked -- read of that exact lane's own now-stale slot,
   entirely independent of the stale-use recovery this session and last session
   both focused on). Re-scope the investigation to
   `widenMaskedAllocaStore`/`Load`'s own per-lane write/read pairing if so.
3. **(if L118 keeps proving hard, per standing next-steps precedent)**: set it
   aside again in favor of L116(a)'s real per-leaf masked load/store
   decomposition (still ~59% of the original L116 sweep's `Fail`s by volume,
   unchanged from prior sessions -- the single highest-value item still on the
   table), L120's `Modf`, or L121's `SIMDize.cpp` divergent-call widening
   generalization (needed for the last `Ldexp` repro case).
4. L116(f)'s ~24 un-root-caused hangs/crashes and L106's
   `pipeline.monolithic.*`/`subgroups.*`/`compute.*` untriaged candidates remain
   untouched across many sessions now.
