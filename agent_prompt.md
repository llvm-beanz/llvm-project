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

1. **(a few hours, largest untriaged cluster, unchanged rank for many
   sessions)** `loops` (30, likely `*_dynamic_iterations` per `L277`'s write-up
   -- two separate bugs already known to block non-leaf cycle linearization
   there) -- still the top pick by size.
2. **(a few hours, new this session)** The 6-case combined-depth-stencil-format
   `fragdepth` bug this session distinguished from the clamp bug:
   `{line,point,triangle}_list_{d24_unorm_s8_uint,d32_sfloat_s8_uint}_no_depth_clamp`.
   Symptom: a covered pixel's depth reads back as `0` (looks unwritten) instead
   of its real shaded value. Likely a `readDepth`/`writeDepth` packed-format
   addressing bug specific to a stencil half being present alongside depth --
   start in `Executor.cpp`'s `readDepth`/`writeDepth` (~line 999) and the
   `D24_UNORM_S8_UINT`/`D32_SFLOAT_S8_UINT` pack/unpack helpers.
3. **(6 cases, feature-decision needed, carried over several sessions)**
   `fragdepth`'s multisample image-creation gap (`*_multisample_{2,4,8}`): CTS's
   own `checkSupport` doesn't account for our `sampledImageDepthSampleCounts =
   1` scoping decision when combined with `VK_IMAGE_USAGE_SAMPLED_BIT`.
   Implement per-sample `OpImageFetch` for depth images (real feature work) or
   find a way to get CTS's own narrower check to catch it as `NotSupported`
   instead of `Fail`.
4. **(small, unexplored across several sessions)** `builtin` (14), `struct` (12)
   -- only became visible as separate clusters once bigger ones cleared.
5. **(small)** `demote` (9), `derivate` (3) -- tiny, likely quick once picked
   up.
6. **(new, 1 case, unexplored)** `texture_gather` -- still flagged, no
   investigation done across 2 sessions now.
7. **(carried over, several sessions running)** `L265`: residual
   `a2b10g10r10_snorm_pack32` ASTC-block-boundary alpha-decode bug in
   `ASTCDecode.cpp`.
8. **(carried over, overdue for many sessions)** `L228(e)`/`(f)`:
   broader-than-glsl/tessellation CTS sampling (`pipeline`'s other sub-suites,
   `api`, `synchronization`) at real scale -- still not done.
9. **(low priority, confirmed unchanged for many sessions)**
   `offload-test-suite`'s own `spec_const_32_bits.test`/`WaveActiveMax.test`
   (failing) and `array_of_matrices.test` (stale `XFAIL:`) -- unrelated to
   FeMe/LLVM, need upstream lit-annotation fixes. Re-verified unchanged this
   session.
10. **(housekeeping, 5 min, next session)** The offload-test-suite `feme`
    branch-drift check should track the cherry-picked hash `99bbd55` (this
    session's own HEAD), not the original `854cc3f`, since `git merge-base
    --is-ancestor 854cc3f HEAD` will now permanently fail even with no real
    drift -- update the check or note both hashes.
