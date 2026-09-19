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

## State for next session

- Working tree clean, HEAD at `325d7d79b0c3` (5 commits this session:
  Fix 1, Fix 2+3, Roadmap, VulkanCTSReport, and this file next).
- `ninja check-feme`: 3,210/3,213 Passed, 3 Unsupported, 0 Failed.
- `ubo.random.*`: 607 Pass / 0 Fail / 1,643 NotSupported.
- `ssbo.*`: 3,195 Pass / 47 Fail / 8,983 NotSupported (the 47 are a
  different, not-yet-triaged bucket -- not L124(o), not investigated
  this session).
- `compute.pipeline.builtin_var.*`: 11/11 Pass.
- New, not-yet-triaged: L124(p), `std140_both` assertion crash, blocks a
  full `ubo.*` sweep. Confirmed pre-existing, confirmed unrelated to
  this session's changes.
- Build directories (`llvm-project/build`, `VK-GL-CTS/build`) left in
  place, warm/incremental. `/tmp/ctsrun` (this session's scratch QPA
  logs) left in place too -- not referenced by anything committed.

## Suggested next steps

1. **(~5 min)** Delete `/tmp/ctsrun` if a future session doesn't need
   this session's raw QPA logs.
2. Triage L124(p) (`std140_both` assertion crash) -- start with
   `gdb -batch -ex run -ex bt --args ./deqp-vk -n
   dEQP-VK.ubo.single_struct.per_block_buffer.std140_both ...` from
   `/tmp/ctsrun` (deqp-vk binary + `vulkan/` data dir already staged
   there) to get the crashing struct shape, then build a minimal
   `feme-opt`-only repro the same way this session did for the Fix-3
   regression.
3. Re-run a full `ssbo.*` sweep's own 47 remaining fails with fresh eyes
   -- not yet individually re-triaged this session (only confirmed the
   aggregate count matches the expected -8 from this session's own
   fix); likely several distinct small bugs, same pattern as L124(i)/(l)
   before it.
4. Once `ubo.*` is unblocked (after L124(p)), run the full sweep (not
   just `ubo.random.*`) for completeness -- L124(o)'s own fix only got
   spot-verified against the `random` subset this session.
