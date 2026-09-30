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

1. **(a few hours, dedicated session, direct continuation, strong lead)**
   `L274`: root-cause the `GroupShared.cpp` SIMDizer gap. Start with
   `FEME_DUMP_IR_PRESIMD` on `add_signed_compute_shared` (the same repro
   `L273` already built, in `/tmp/atomicrepro/` if still present, or rebuild
   it the same way) to see the exact nested-GEP shape being rejected, then
   decide whether widening `rewriteGroupSharedGlobals`'s existing
   second-level-GEP support to also accept a *uniform* (non-vector-typed)
   nested access chain -- not just the divergent-vector one it already
   handles -- clears the 62/64 majority; the 2/64 "divergent aggregate"
   outliers (likely `comp_swap` variants, not yet confirmed) may need a
   separate fix.
2. **(a few hours, second-largest, ready to pick up)** `shader_expect_assume`
   (51 cases) -- still not started across multiple sessions now.
3. **(unknown, filed, not started)** `440.linkage.varying` (49), `loops` (30,
   likely `*_dynamic_iterations`) -- worth a joint triage session.
4. **(small, unexplored across several sessions)** `builtin_var` (21),
   `struct` (16), `builtin` (14), `demote` (9), `derivate` (3),
   `logical_copy` (2) -- all still untouched.
5. **(carried over, several sessions running)** `L265`: residual
   `a2b10g10r10_snorm_pack32` ASTC-block-boundary alpha-decode bug in
   `ASTCDecode.cpp`.
6. **(carried over, overdue for many sessions)** `L228(e)`/`(f)`:
   broader-than-glsl/tessellation CTS sampling (`pipeline`'s other
   sub-suites, `api`, `synchronization`) at real scale -- still not done.
7. **(low priority, not a regression, flagged many sessions now)**
   `offload-test-suite`'s own `Feature/SpecializationConstant/
   spec_const_32_bits.test`/`WaveOps/WaveActiveMax.test` (failing) and
   `Feature/PushConstant/array_of_matrices.test` (stale `XFAIL:`) --
   confirmed unchanged again this session.
