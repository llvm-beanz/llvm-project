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
  `105d572ad9cd`.
- `ssbo.*` down to **1 fail of 12,225 (0.008%)**:
  `dEQP-VK.ssbo.layout.random.nested_structs_instance_arrays.8`
  ("Result comparison and counter values are incorrect").
- Investigated this residual: it's a considerably more complex shader
  (3 blocks, 6 nested struct types `sA`-`sF`, several matrix shapes).
  Built a faithful repro of its own `BlockD.n[]` member (a *square*
  `mat3` runtime array, matching this test's exact real offsets/
  decorations including a preceding `sF{bool}` struct member) and
  confirmed **this session's own fix already handles it correctly** --
  ruling that member out. The real mismatch is elsewhere in the
  shader, not yet isolated.
- No feature/extension inventory changes needed (internal correctness
  fix, no new Vulkan surface) -- verified, not just assumed.
- Build directories left in place, warm/incremental. `/tmp/ctsrun` has
  this session's fresh scratch logs (`l124t_41.qpa`/`.stdout`,
  `l124t_confirm.qpa`/`.stdout`, `l124t_nsia8.qpa`/`.stdout`,
  `l124t_triage1.qpa`/`.stdout`, `ssbo_l124t.qpa`/`.stdout`,
  `ubo_random_l124t.qpa`/`.stdout`) and `/tmp/l124t_*.mlir` repros
  (`blockB_repro`, `blockC_repro` -- the one that found the bug,
  `mat3_repro`, `blockD_full` -- confirms the fix already covers
  `nested_structs_instance_arrays.8`'s own `BlockD.n[]`, `minimal` --
  redundant with the committed lit test) -- none referenced by
  anything committed.

## Suggested next steps

1. **(~5 min)** Delete `/tmp/l124u_*` and `/tmp/ctsrun/l124u_*`/
   `/tmp/ctsrun/l124t_*` scratch files -- none referenced by anything
   committed. The hand-rolled verify harness (`/tmp/l124u_verify.py`,
   `/tmp/l124u_verify2.py`, `/tmp/l124u_build_heap.py`) is not needed
   again now that the real CTS test passes.
2. **The entire L124 roadmap series is now closed** (`ssbo.*` and
   `ubo.*` both fully clean, 0 `Fail` each). There is no obvious next
   L124 sub-item to pick up -- check `Roadmap.md` for the next
   unstarted milestone item instead (search for the next un-struck-
   through `P2`/`P1` row near where L124 was).
3. `ninja check-feme` and the CTS build directories are both
   incremental from here -- reuse them, no reconfigure needed.
4. If a future session wants extra confidence beyond `ssbo.*`/
   `ubo.random.*`, consider a full `ubo.*` sweep (13,240 cases, not run
   this session since no `Uniform`/`Block`-specific code was touched)
   -- low priority, this session's fix only touches struct-member-index
   remapping shared by both `ubo.*` and `ssbo.*` paths, and
   `ubo.random.*` already confirms no regression there.
5. Consider whether the array-of-struct case this fix deliberately left
   out of scope (`RealStructTy` resets to null across Array/
   RuntimeArray levels) could hide the same bug class for a struct
   nested *inside* an array -- not tested this session, not currently
   known to be a live CTS failure (both families are fully clean), so
   low priority unless a future regression surfaces there.

