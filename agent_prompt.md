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

1. **L124** (~1-2 hours to scope, unknown to fix): triage `compute.*`'s
   remaining 16 `Fail`s and `ssbo.*`'s remaining 905 `Fail`s. `ssbo.*`'s 905 is
   large enough it's likely several distinct bugs, not one -- bucket by failing
   case name before picking a first repro, the same way L116's original
   `graphicsfuzz.*` sweep did.
2. **L125** (~1 hour to scope): first triage pass of `pipeline.monolithic.*`
   (465,554 cases, never sampled). Run a representative sample of its own
   subfamilies, bucket failures, pick a first concrete repro.
3. **L126** (~30 min): finish `subgroups.ballot_broadcast.*`'s sweep, abandoned
   mid-read this session when focus shifted to `compute.*`. Likely folds into
   "no real bugs in `subgroups.*`" but not yet confirmed for this specific
   subfamily.
4. **L116(f)** (no time estimate, several sessions untouched): ~24
   un-root-caused hangs/crashes in `graphicsfuzz.*`. Consider the
   runtime-instrumentation technique that broke L118 open (a
   `feme.cpu.debug.print.*`-style host callback) rather than more manual IR
   tracing.

## State for next session

- Working tree clean, 6 new commits this session (fix, lit-test updates,
  DXIL-raising fix, new regression test, CTS report, roadmap) plus this entry's
  own commit = 7 total.
- `ninja check-feme`: 3,201/3,204 Passed, 3 Unsupported, 0 Failed.
- `compute.*` baseline for next session: **669 Pass / 16 Fail / 60,775
  NotSupported** (of 61,460).
- `ssbo.*` baseline for next session: **2,337 Pass / 905 Fail / 8,983
  NotSupported** (of 12,225).
- `graphicsfuzz.*` baseline unchanged from last session: 601 Pass / 124 Fail / 8
  NotSupported (of 733) -- not re-swept this session, since this session's fix
  didn't touch anything on that path.
- No scratch files left in `/tmp` from this session.
