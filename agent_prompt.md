---
model: claude-sonnet-5
resume: 6e011932-a46a-43ae-97b3-283c96c999ff
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

- Working tree clean before this file's own commit, HEAD at
  `dc984fe81d76`.
- `ninja check-feme`: 3,211/3,214 Passed, 3 Unsupported, 0 Failed.
- `ubo.*` **full** sweep: 5,687 Pass / 0 Fail / 7,553 NotSupported (of
  13,240) -- first time this sweep has ever run clean to completion.
- `ssbo.*`: 3,195 Pass / 47 Fail / 8,983 NotSupported -- unchanged from
  before this session, confirmed same 47 test names, not L124(p)-related.
- No feature/extension inventory changes needed (internal correctness
  fix, no new Vulkan surface) -- verified, not just assumed.
- Build directories (`llvm-project/build`, `VK-GL-CTS/build`) left in
  place, warm/incremental. `/tmp/ctsrun` has this session's own fresh
  scratch logs (`std140_both.qpa`, `single_struct.qpa`, `ubo_full.qpa`,
  `ssbo_full.qpa`, `ssbo_triage1.qpa` + their `.stdout` companions) --
  not referenced by anything committed.

## Suggested next steps

1. **(~5 min)** Delete `/tmp/ctsrun`'s scratch logs from this session
   if a future session doesn't need the raw QPA output (`std140_both.qpa`,
   `single_struct.qpa`, `ubo_full.qpa`/`.stdout`, `ssbo_full.qpa`/
   `.stdout`, `ssbo_triage1.qpa`) -- not referenced by anything
   committed.
2. Start L124(q) (`ssbo.*`'s remaining 47 `layout.random.*` fails) --
   the full list of 47 test names is in this session's own
   `/tmp/ctsrun/ssbo_fails.txt` if still present, otherwise regenerate
   with `./deqp-vk -n "dEQP-VK.ssbo.*" ...` and
   `grep -B1 "^  Fail" | grep "Test case"`. Start with
   `dEQP-VK.ssbo.layout.random.basic_types.18` (already confirmed to
   fail with "Counter value incorrect", not a crash) and build a
   `FEME_DUMP_IR=1` trace the same way L124(l)/(n) did.
3. `ninja check-feme` and `ninja deqp-vk` are both incremental from here
   -- reuse the existing build directories, no reconfigure needed.
