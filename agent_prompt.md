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

**Always**: Make sure you are not using precompiled headers in your build.

# Request

Please continue working on the FeMe Vulkan ICD. The previous session's suggested
next steps are:

1. **(a few hours, dedicated session)** The 8 remaining
   `ifblock`/`elseblock` cases. Start: rebuild with
   `FEME_DEBUG_LINEARIZE_TRACE=1` and a temporary
   `F.print(errs())` right after the `OtherCondBrBlocks`/
   `DivergentCandidates` trace print in `linearizeCycle`
   (`feme/lib/Transforms/CPU/Linearize.cpp`, search for `L277TRACE`), run
   `dEQP-VK.glsl.loops.special.for_dynamic_iterations.ifblock_fragment`
   directly via `deqp-vk`. You'll see: `Header`'s own `CondBr` is
   genuinely divergent, has no `ExitCheck` at all (neither direct nor
   relay-recovered), and one of its two successors eventually reaches
   the *same* `Flow` block that `OtherCondBrBlocks`'s own entry already
   names as `CheckBlock`. Needs a new code path: when `Header`'s own
   divergent branch and `CheckBlock`'s divergent branch converge on the
   *same* physical dispatch block (rather than two independently-relayed
   exits), both conditions need to be combined/reasoned about together
   before `CheckBlock`'s own phi-based dispatch logic runs -- likely
   requires peeling or re-deriving `Header`'s own condition from
   `CheckBlock`'s own phi inputs, not just narrowing a mask sequentially
   the way the two now-fixed shapes did.
2. **(1-2 hours)** `L293`: `derivate`'s 3 residual failures
   (`fwidth{,coarse,fine}.fbo_float.vec4_highp`). Extract exact pixel
   values first (via `--deqp-log-images=enable` or a small repro) to
   tell a tiny rounding delta (no fix needed, like `L287`'s `cosh`/`sinh`)
   from a real bug.
3. **(6 cases, feature decision needed, carried over many sessions)**
   `fragdepth` multisample image-creation gap (`*_multisample_{2,4,8}`).
4. **(6 cases, carried over many sessions)** `fragdepth`
   combined-depth-stencil-format bug: `Executor.cpp`'s
   `readDepth`/`writeDepth` (~line 999).
5. **(carried over many sessions)** `L265`: residual
   `a2b10g10r10_snorm_pack32` ASTC-block-boundary alpha-decode bug in
   `ASTCDecode.cpp`.
6. **(overdue many sessions)** Broader-than-glsl/tessellation CTS
   sampling at real scale (`pipeline`'s other sub-suites, `api`,
   `synchronization`). This session only re-ran `loops`/`demote`
   (targeted, since that's what regressed) -- a full `dEQP-VK.glsl.*` run
   was started but takes much longer than one session's budget; kill any
   future full-cluster attempt early and instead run small, bounded,
   genuinely-isolated sub-groups.
7. **(low priority)** `offload-test-suite`'s own
   `spec_const_32_bits.test`/`WaveActiveMax.test`/`array_of_matrices.test`
   lit-annotation issues -- unrelated to FeMe/LLVM, needs upstream fixes.
8. **(housekeeping, overdue)** `check-hlsl-feme-vk` and the `feme`
   branch-drift check -- not re-run this session (focus was entirely on
   the `loops.*` regression); do this early next session.
