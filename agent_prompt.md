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

1. **L124(g)** (~half a day+ to scope a first repro, unknown to fix): `ssbo.*`'s
   572-case wrong-numeric-result bucket, 63% of all `ssbo.*` fails and likely
   the single highest-value item across the whole L124 breakdown. 735 of 905
   `ssbo.*` fails are matrix-typed by name -- strongly suggests a systemic
   std140/std430 matrix layout/stride bug analogous to L123's vec3-stride fix.
   Start by picking one small, single-matrix repro (e.g.
   `dEQP-VK.ssbo.layout.single_basic_type.std140.row_major_mat3`) and tracing
   actual vs. expected byte layout, the same way L123's `CommandBufferTest.cpp`
   repro worked.
2. **L124(e)** (~half a day): add the missing
   `feme.cpu.resource.store.raw.i8`/`v{2,3,4}i8` runtime-function variants and
   their JIT-symbol registration, mirroring the existing `i16` variant's shape
   exactly. Unblocks 274 of 905 `ssbo.*` fails (30%) -- though some may have a
   second, independent bug hiding behind this one once unblocked, not yet
   confirmed.
3. **L124(f)** (~half a day, needs its own root-cause pass first):
   `spirv.AccessChain` into a `RowMajor`-decorated matrix nested inside a
   runtime array fails legalization (36 cases) -- `ColMajor` in the same
   position is fine, so the gap is specific to `RowMajor`'s own row-vs-column
   addressing arithmetic.
4. **L124(a)** (~half a day): `read_unbound_ssbo` -- `ArrayLengthPattern`'s
   member-index check needs relaxing from "must be 0" to "must be the struct's
   own last member", plus a new runtime-call variant (or operand) to subtract a
   fixed prefix byte offset before dividing by stride, since the existing
   `femeCpuResourceGetDimensionsRawI32` has no way to do that without breaking
   the "unbound descriptor returns 0" contract. Full design already scoped this
   session -- see `Roadmap.md`'s L124(a) row for the exact plan.
5. **L124(b)/(c)** (~half a day+ each): `remove_global_load_pass` (new
   `spirv.GlobalVariable` initializer-attribute) and `undefined_values` (new
   `spirv.CopyLogical` op) -- both real dialect additions, scoped above.
6. **L124(d)** (~half a day to scope, unknown to fix):
   `device_group.device_index` -- `gl_DeviceIndex` unwired for compute
   pipelines. Needs research into whether `VK_KHR_device_group` is otherwise
   supported by feme's Vulkan layer before estimating; may be as simple as
   always reporting `DeviceIndex = 0`.
7. **L125**/**L126**/**L116(f)** all remain untouched, standing fallbacks from
   prior sessions.

## State for next session

- Working tree clean, 4 new commits this session (matrix `OpConstantNull` fix,
  `OpName`/`NClamp` fix, roadmap/CTS-report update) plus this entry's own commit
  = 5 total.
- `ninja check-feme`: 3,201/3,204 Passed, 3 Unsupported, 0 Failed.
- `compute.*` baseline for next session: **679 Pass / 6 Fail / 60,775
  NotSupported** (of 61,460) -- the 6 remaining are L124(a)-(d) plus the 2
  pre-existing `containsAddressableBool` cases (out of scope).
- `ssbo.*` baseline for next session: **2,337 Pass / 905 Fail / 8,983
  NotSupported** (of 12,225), bucketed but unchanged in count -- see the
  L124(e)-(h) breakdown above for exact bucket sizes.
- No scratch files left in `/tmp` from this session.
