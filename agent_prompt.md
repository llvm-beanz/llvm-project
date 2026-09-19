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

## State for next session

- Working tree clean, 3 new commits this session (core fix, test,
  Roadmap/CTSReport update) plus this entry's own commit = 4 total.
- `ninja check-feme`: 3,208/3,211 Passed, 3 Unsupported, 0 Failed.
- `ssbo.*` baseline for next session: **3,150 Pass / 92 Fail / 8,983
  NotSupported** (of 12,225) -- up from 3,063/179/8,983.
- `compute.*` baseline for next session: **679 Pass / 6 Fail / 60,775
  NotSupported** (of 61,460) -- unchanged, confirmed by a full re-sweep this
  session.
- `/tmp` scratch cleaned up.

## Next steps

1. **L124(l)** (~half a day to re-triage, now the *only* remaining named
   `ssbo.*` bucket): `random` (64), `unsized_nested_struct_array` (24), and
   4 `unsized_array_length.*` singletons (`float_{no_offset,offset}_
   {explicit_size,whole_size}`) -- these 92 cases are everything left in
   `ssbo.*`. `random` is likely a mix of whatever's left once the other two
   are individually reduced (it's CTS's own fuzz-shaped family, drawing
   from every other feature), so start with `unsized_nested_struct_array`
   or the 4 singletons first -- smaller, more likely a single distinct root
   cause each.
2. **L124(a)/(b)/(c)/(d)/L125/L126/L116(f)** all remain untouched, standing
   fallbacks from prior sessions -- see `Roadmap.md` for each row's own
   scoping.
3. With `ssbo.*` down to 92 fails (from 651 seven sessions ago, now under
   1% of the whole `ssbo.*` suite), the next session should prioritize
   L124(l)'s 4 `unsized_array_length.*` singletons first -- smallest,
   likely fastest win, and they've been carried over unfixed since before
   L124(g) (at least 5 sessions) without ever getting their own dedicated
   trace.
