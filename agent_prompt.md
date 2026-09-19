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
  `e06a67389ffc`.
- `ninja check-feme`: 3,214/3,217 Passed, 3 Unsupported, 0 Failed.
- `ssbo.*`: **3,238 Pass / 4 Fail / 8,983 NotSupported** (of 12,225) --
  down from 3,232/10/8,983 at session start.
- `ubo.random.*`: 607/0/1,643, unchanged, confirmed no regression. Full
  `ubo.*` (13,240 cases) not re-run this session -- not needed, no
  `Uniform`/`Block`-specific code touched.
- No feature/extension inventory changes needed (internal correctness
  fix, no new Vulkan surface) -- verified, not just assumed.
- Build directories (`llvm-project/build`, `VK-GL-CTS/build`) left in
  place, warm/incremental. `/tmp/ctsrun` has this session's own fresh
  scratch logs (`ssbo_l124s.qpa`/`.stdout`, `l124s_confirm.qpa`/`.stdout`,
  `ubo_random_l124s.qpa`/`.stdout`) plus older scratch from prior
  sessions (`l124s_41.qpa`, `l124s_shape.qpa`, `l124s_verify.qpa`,
  `l124s_verify2.qpa`) and this session's own dead-end repros
  (`/tmp/l124t_repro.mlir`, the wrapper-shape repro that turned out not
  to match the real failing test) -- none referenced by anything
  committed.

## Suggested next steps

1. **(~5 min)** Delete `/tmp/ctsrun`'s scratch logs and `/tmp/l124s_*.mlir`/
   `/tmp/l124t_repro.mlir`/`/tmp/l124u_repro.mlir` if a future session
   doesn't need them -- none are referenced by anything committed.
   Keep `/tmp/l124s_repro3.mlir` only if useful as a reference for the
   now-fixed shape (it's also captured permanently in the new lit test,
   so not strictly needed either).
2. Start **L124(t)** (`ssbo.*`'s remaining 4 fails). Recommended order,
   cheapest/most-isolated first:
   - `all_shared_buffer.13` and `nested_structs_instance_arrays.8`: pull
     their real shapes via `--deqp-log-decompiled-spirv=enable` the same
     way every prior L124 triage did -- these were previously bundled
     into an "unexplained `ac_numPassed`" bucket of 6 that this
     session's fix collaterally reduced to 2, so they may share a
     related (but not identical) root cause worth checking first.
   - `all_shared_buffer.41`: **do not restart from the wrapper-shape
     assumption** -- confirmed this session that the real block has 4
     members and is NOT the wrapper shape; a faithful repro of the real
     shape converts correctly at the IR level. The bug (if it's a
     compiler bug at all, as opposed to a CTS/driver-level issue) is
     likely downstream of `feme-opt`'s own output -- consider tracing
     with the actual `feme`/JIT runtime path, or checking interaction
     with a sibling struct member's own layout, rather than more
     `feme-opt`-only repros.
   - `all_per_block_buffers.20`'s own pipeline-creation crash
     (`VK_ERROR_INITIALIZATION_FAILED`) is its own separate
     investigation -- likely needs a debugger attached to the
     pipeline-creation call, not a CTS-log trace.
3. `ninja check-feme` and `ninja deqp-vk` are both incremental from here
   -- reuse the existing build directories, no reconfigure needed.
4. With `ssbo.*` down to 4 of 12,225 (0.03%) and `ubo.random.*` fully
   clean, L124(t) closing this last small bucket would put the `ssbo.*`
   family fully clean too -- worth prioritizing, though each of the 3
   remaining issues may need its own dedicated debugging session (a
   crash needs a debugger, not a CTS trace).
