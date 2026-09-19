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

1. **L124(g)** (~half a day+, lead already found this session): confirm
   `Access->Layout.Stride`'s actual value in
   `RowMajorMatrixStorePattern::matchAndRewrite` for `row_major_mat2` via a
   temporary debug dump before changing anything (see lead above). Once
   confirmed, the fix is likely narrow (either `getMatrixWholeAccess` reads the
   wrong decoration/field, or a distinct arrays-of-matrices code path needs the
   same `MatrixStride`-vs-natural-size padding math the single-matrix case
   already has). This is still the single highest-value remaining item in the
   whole L124 breakdown (555 of 651 `ssbo.*` fails, 85%, are matrix-typed).
2. **L124(f)** (~half a day, needs its own root-cause pass first):
   `spirv.AccessChain` into a `RowMajor`-decorated matrix nested inside a
   runtime array fails legalization (36 cases) -- `ColMajor` in the same
   position is fine.
3. **L124(a)** (~half a day): `read_unbound_ssbo` -- design already scoped in a
   prior session, see `Roadmap.md`'s L124(a) row.
4. **L124(b)/(c)** (~half a day+ each): `remove_global_load_pass` (new
   `spirv.GlobalVariable` initializer-attribute) and `undefined_values` (new
   `spirv.CopyLogical` op) -- both real dialect additions.
5. **L124(d)/L124(h)/L125/L126/L116(f)** all remain untouched, standing
   fallbacks from prior sessions.

## State for next session

- Working tree clean, 3 new commits this session (L124(e) fix+test,
  Roadmap/CTSReport update, L124(g) lead scoping) plus this entry's own commit =
  4 total.
- `ninja check-feme`: 3,202/3,205 Passed, 3 Unsupported, 0 Failed (was
  3,201/3,204 -- +1 Pass from this session's new unit test).
- `ssbo.*` baseline for next session: **2,591 Pass / 651 Fail / 8,983
  NotSupported** (of 12,225) -- up from 2,337/905/8,983.
- `compute.*` baseline for next session: **679 Pass / 6 Fail / 60,775
  NotSupported** (of 61,460) -- unchanged, confirmed by a full re-sweep this
  session.
- No scratch files left in `/tmp` from this session.
