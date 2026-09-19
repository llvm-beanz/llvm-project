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

## State right now

- Working tree clean after this entry's own commit, 4 new commits this
  session total.
- `ninja check-feme`: 3,208/3,211 Passed, 3 Unsupported, 0 Failed.
- `ssbo.*`: **3,187 Pass / 55 Fail / 8,983 NotSupported** (of 12,225) --
  all 55 remaining fails are in `random`.
- `compute.*`: **679 Pass / 6 Fail / 60,775 NotSupported** (of 61,460) --
  unchanged.
- `/tmp` scratch cleaned up (this session's own; a large pile of prior-
  session leftovers in `/tmp` still untouched, not from this session).

## Next steps

1. **L124(o)** (~half a day, needs its own `FEME_DUMP_IR=1` trace):
   `random`'s residual 55 fails. Quick message-only triage (no deep
   trace yet) found 3 distinct shapes still mixed in:
   - 25 "Result comparison and counter values are incorrect"
   - 15 "Counter value incorrect"
   - 14 "Result comparison failed"
   - 1 `VK_ERROR_INITIALIZATION_FAILED` (pipeline-creation failure, not
     a runtime miscompile -- needs `FEME_VULKAN_LOG_CREATION_ERRORS=1`)

   Start with the two "counter" buckets (40 of 55 combined) -- both
   mention an SSBO atomic counter specifically, most likely one shared
   root cause distinct from anything fixed so far (this session's fix
   was a plain load/store bug, not atomics-related).
2. **L124(a)/(b)/(c)/(d)/L125/L126/L116(f)** all remain untouched,
   standing fallbacks from prior sessions -- see `Roadmap.md` for each
   row's own scoping.
3. `ssbo.*` is now at 0.45% fail rate (55 of 12,225), down from 651 nine
   sessions ago -- L124(o) is very likely the last item standing before
   a fully clean `ssbo.*` sweep. Prioritize it first next session.
