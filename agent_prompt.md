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
  `a9fa298d0409`.
- `ninja check-feme`: 3,213/3,216 Passed, 3 Unsupported, 0 Failed.
- `ssbo.*`: **3,232 Pass / 10 Fail / 8,983 NotSupported** (of 12,225) --
  down from 3,230/12/8,983 at session start.
- `ubo.random.*`: 607/0/1,643, unchanged, confirmed no regression.
- No feature/extension inventory changes needed (internal correctness
  fix, no new Vulkan surface) -- verified, not just assumed.
- Build directories left in place, warm/incremental. `/tmp/ctsrun` has
  this session's own fresh scratch logs plus two abandoned minimal
  repros (`/tmp/l124s_repro1.mlir`, `/tmp/l124s_repro2.mlir`,
  `/tmp/l124s_repro3.mlir`) that reproduce the type-legalization gap --
  worth keeping for next session's own L124(s) type-level investigation
  rather than deleting.

## Suggested next steps

1. **(~5 min)** Delete `/tmp/ctsrun`'s CTS-run scratch logs from this
   session if a future session doesn't need the raw QPA output. Keep
   `/tmp/l124s_repro3.mlir` (the working minimal repro of the
   type-legalization gap) -- it saves rebuilding it from scratch.
2. Start **L124(s)**'s type-level sub-class first (3 of the 10 fails:
   `all_shared_buffer.{41,44}`, `nested_structs_arrays.14`) --
   `convertOffsetStructTypeIgnoringDecorations`/
   `convertArrayTypeIgnoringDecorations` need to widen a matrix nested
   inside a non-wrapper array-of-struct member's own type. Start from
   `/tmp/l124s_repro3.mlir`'s own `spirv.GlobalVariable` legalization
   failure and trace which conversion function declines it.
3. Then the 6 `ac_numPassed = 0, expected 1` fails
   (`all_per_block_buffers.47`, `all_shared_buffer.{1,13,17}`,
   `nested_structs.16`, `nested_structs_instance_arrays.8`) -- not
   decoded at all yet. Start with `--deqp-log-decompiled-spirv=enable`
   on one of them the same way every prior L124 triage did.
4. `all_per_block_buffers.20`'s own `VK_ERROR_INITIALIZATION_FAILED`
   pipeline-creation crash is its own separate investigation (a
   compiler crash, not a data mismatch) -- likely needs a debugger
   attached to the pipeline-creation call, not a CTS-log trace. Lowest
   priority of the 3 classes since it's a single isolated case.
5. `ninja check-feme` and `ninja deqp-vk` are both incremental from here
   -- reuse the existing build directories, no reconfigure needed.
