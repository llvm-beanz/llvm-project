---
model: claude-sonnet-5
resume: 1ebfe39e-5f48-4c14-810b-08a0301888a7
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

**When you find an issue outside FeMe**: Create an isolated reproducer, fix it,
and apply the fix in a commit that only touches files from outside the FeMe
subdirectory. Ensure that fixes to other LLVM sub-projects are self-contained
and tested.

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

# Request

Can you continue working on the FeMe ICD implementation? The previous session's
suggested next steps are:

1. **(a few hours, dedicated session, strong lead)** Root-cause `L259`
   via the `mat4x3` reproduction found this session
   (`dEQP-VK.glsl.matrix.add.dynamic.highp_mat4x3_float_fragment` is
   the simplest case -- fixed shape, no dynamic indexing needed to
   trigger it, unlike `L259`'s own original `indexing.varying_array`
   cases). Start with `FEME_DUMP_IR`/`FEME_DUMP_IR_PRESIMD` on this
   case vs. a passing `mat4x2`/`mat4x4` sibling to see exactly where a
   `vec3`-row matrix's row/component count diverges from the 4-lane
   assumption (`SignatureElement::FirstComponent`'s own comment,
   `L256`). If confirmed as the same bug, this single fix should clear
   **104 cases** (12 `indexing` + 92 `matrix`) at once -- the single
   highest-leverage fix available right now.
2. **(check first)** Confirm the full `dEQP-VK.glsl.*` sweep
   (`/tmp/l270_271_full_sweep.log`/`.qpa`) finished with the predicted
   ~19,056 Pass / ~401 Fail / 8,963 NotSupported; re-tally the residual
   Fail list fresh (numbers shift slightly session to session) before
   picking the next cluster.
3. **(unknown, filed, not started)** Remaining `atomic_operations`
   residual (64 Fail, down from 96) -- believed to be the runtime
   `Image mismatch` half rather than pipeline-creation rejections, not
   yet individually triaged.
4. **(unknown, filed, not started)** `shader_expect_assume` (51 cases),
   `440`/`linkage.varying` (49), `conversions` (30), `loops` (30) --
   smaller clusters from last session's tally, likely still roughly
   this size; re-tally against the fresh sweep first (item 2 above).
5. **(carried over, several sessions running, still not picked up)**
   `L265`: residual `a2b10g10r10_snorm_pack32` ASTC-block-boundary
   alpha-decode bug in `ASTCDecode.cpp`.
6. **(carried over, overdue for many sessions)** `L228(e)`/`(f)`:
   broader-than-glsl/tessellation CTS sampling (`pipeline`'s other
   sub-suites, `api`, `synchronization`) at real scale -- still not
   done. Every recent session (including this one) has run only
   `dEQP-VK.glsl.*`/`dEQP-VK.tessellation.*` sweeps; this remains a
   real blind spot.
7. **(low priority, not a regression, flagged many sessions now)**
   `offload-test-suite`'s own
   `Feature/SpecializationConstant/spec_const_32_bits.test`/
   `WaveOps/WaveActiveMax.test` (failing) and
   `Feature/PushConstant/array_of_matrices.test` (stale `XFAIL:`) still
   need upstream lit-annotation fixes -- unrelated to FeMe/LLVM,
   confirmed unchanged again this session.
8. No git stashes left open this session (none were used -- both repos
   were clean/in-sync throughout, confirmed via the mandatory
   session-start checks; re-confirmed at session end below).
