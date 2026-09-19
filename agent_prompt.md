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

## Next steps

1. **L124(j)** (~half a day, arithmetic pattern already known from this
   session's own fix): `rewriteBlockAccess`'s partial-access branch
   (column-select/scalar-element) needs the same nesting-depth generalization
   `getMatrixWholeAccess` just got, applied to its own `SelectedType` check and
   `Selector+1`/`+2`/`+3` index arithmetic. Also closes `single_basic_array`'s
   pre-existing 36-case RowMajor-column-select gap (same code path at nesting
   depth 1). Covers 180 of the remaining 395 `ssbo.*` fails (144 + 36).
2. **L124(k)** (~half a day to scope): `instance_array_basic_type`'s 84
   remaining fails include whole-access failures L124(i) didn't close — needs
   its own `FEME_DUMP_IR=1` trace to confirm whether its content shape (array of
   block instances) is a variant of the same bug or something new, before
   assuming either L124(i) or L124(j)'s fix applies.
3. **L124(l)** (~half a day to re-triage): `random` (67), `basic_unsized_array`
   (36), `unsized_nested_struct_array` (24), `unsized_array_length.*` (4
   singletons) — not re-triaged this session; some may already be absorbed by
   L124(j)/(k) once those land.
4. **L124(f)/(a)/(b)/(c)/(d)/L125/L126/L116(f)** all remain untouched, standing
   fallbacks from prior sessions.

## State for next session

- Working tree clean, 2 new commits this session (core fix + test,
  Roadmap/CTSReport update) plus this entry's own commit = 3 total.
- `ninja check-feme`: 3,204/3,207 Passed, 3 Unsupported, 0 Failed (was
  3,203/3,206 — +1 Pass from this session's new lit test).
- `ssbo.*` baseline for next session: **2,847 Pass / 395 Fail / 8,983
  NotSupported** (of 12,225) — up from 2,729/513/8,983.
- `compute.*` baseline for next session: **679 Pass / 6 Fail / 60,775
  NotSupported** (of 61,460) — unchanged, confirmed by a full re-sweep this
  session.
- No scratch files left in `/tmp` from this session.
