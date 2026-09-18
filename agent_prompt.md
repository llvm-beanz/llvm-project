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

1. **(~30 min)** Build a *minimal* reduced repro instead of using the full
   GraphicsFuzz shader: a fragment shader with (a) an aggregate `Private`
   global, (b) a real but always-false discard inside a uniform-condition
   branch, (c) a read of that global after the discard branches merge back,
   feeding directly into the output color. Much easier to hand-trace or
   instrument than the current repro.
2. **(~30 min)** Add a temporary runtime print (feme's CPU backend can call into
   a real host function — check `feme/lib/Target/CPU/` for an existing
   debug-print intrinsic, or add one) right before the block-52-style
   `extractelement` in `SIMDize.cpp`, printing the full mask vector and gather
   result for all 4 lanes at actual runtime. This replaces further manual
   tracing with ground truth.
3. **(~half a day, blocked on #1/#2)** Once a concrete lane/mask mismatch is
   observed at runtime, implement the actual fix: most likely, thread through
   the masked gather's own `EffectiveMask` (or the `WideMask` operand) so the
   stale-use recovery can pick a lane whose mask bit is dynamically known set
   (e.g. `llvm.cttz` over the mask cast to an integer), instead of hardcoding
   lane 0.
4. If L118 keeps proving harder than a half-day/day budget after step 2's
   instrumentation, it's reasonable to **set it aside again** and pick from the
   other still-untouched items: L116(a)'s real per-leaf masked load/store
   decomposition (still ~59% of the original L116 sweep's `Fail`s, unchanged
   from prior sessions), L120's `Modf`, L121's `SIMDize.cpp` divergent-call
   widening generalization, or L116(f)'s ~24 un-root-caused hangs/crashes.
5. Nobody has picked up L106's `pipeline.monolithic.*`/`subgroups.*`/`compute.*`
   untriaged candidates across many sessions now — still on the table whenever
   L116/L117/L118 close or get set aside.
