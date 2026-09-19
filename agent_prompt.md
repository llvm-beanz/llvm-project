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
  `e1b26d5ba29c`.
- `ninja check-feme`: 3,212/3,215 Passed, 3 Unsupported, 0 Failed.
- `ssbo.*`: **3,230 Pass / 12 Fail / 8,983 NotSupported** (of 12,225) --
  down from 3,195/47/8,983 at session start.
- `ubo.random.*`: 607/0/1,643, unchanged, confirmed no regression. Full
  `ubo.*` (13,240 cases) not re-run this session -- not needed, no
  `Uniform`/`Block`-specific code touched.
- No feature/extension inventory changes needed (internal correctness
  fix, no new Vulkan surface) -- verified, not just assumed.
- Build directories left in place, warm/incremental. `/tmp/ctsrun` has
  this session's own fresh scratch logs (`ssbo_full.qpa`/`.stdout`,
  `triage1.qpa`, `triage1b.qpa`, `ssbo_full2.qpa`/`.stdout`,
  `ubo_random_check.qpa`, `triage_r1.qpa`) -- not referenced by anything
  committed.

## Suggested next steps

1. **(~5 min)** Delete `/tmp/ctsrun`'s scratch logs from this session
   if a future session doesn't need the raw QPA output.
2. Start **L124(r)** (`ssbo.*`'s remaining 12 `layout.random.*` fails:
   `all_per_block_buffers` 2, `all_shared_buffer` 5, `nested_structs` 2,
   `nested_structs_arrays` 2, `nested_structs_instance_arrays` 1). One
   already spot-checked this session
   (`dEQP-VK.ssbo.layout.random.nested_structs.12`, "Result comparison
   failed" -- a different message than L124(q)'s own repro, suggesting a
   distinct bug, not confirmed). Use `--deqp-log-decompiled-spirv=enable`
   the same way this session did to pull the real struct shape straight
   from the qpa log, then build a minimal `feme-opt` repro before
   assuming a shared root cause across all 12.
3. `ninja check-feme` and `ninja deqp-vk` are both incremental from here
   -- reuse the existing build directories, no reconfigure needed.
4. With `ssbo.*` down to 12 of 12,225 (0.1%) and `ubo.*` fully clean
   (0 Fail of 13,240), L124(r) closing this last small bucket would put
   both the `ubo.*` and `ssbo.*` CTS families at a **fully clean** state
   -- worth prioritizing over any other open roadmap item next session.
