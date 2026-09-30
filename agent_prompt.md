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

1. **(small-medium, likely 1-2 hours, high value: fully closes `loops`)**
   Root-cause the remaining 4-case `dowhile_trap` hang:
   `special.{for,while}_dynamic_iterations.dowhile_trap_{fragment,vertex}`.
   Narrowly scoped now (not `do_while`, only `for`/`while`
   dynamic-iteration-count loops combined with the `dowhile_trap` shader shape)
   -- a much smaller repro surface than before. Likely still related to whatever
   gap the reverted `isCycleHeaderBranch` attempt was chasing; worth a fresh,
   careful look now that the mask-threading/non-leaf-traversal fixes have
   changed the surrounding code.
2. **(small, unexplored across several sessions)** `builtin` (14), `struct` (12)
   -- only became visible as separate clusters once bigger ones cleared.
   `builtin` may include the 8 `cosh`/`sinh` precision fails an earlier partial
   sweep found (not confirmed) -- re-tally cleanly using the isolated-per-case
   methodology from item 7 above before trusting any number here.
3. **(small)** `demote` (9), `derivate` (3) -- tiny, likely quick once picked
   up.
4. **(new, 1 case, unexplored across 3 sessions now)** `texture_gather` -- still
   flagged, no investigation done.
5. **(6 cases, feature-decision needed, carried over several sessions)**
   `fragdepth`'s multisample image-creation gap (`*_multisample_{2,4,8}`): CTS's
   own `checkSupport` doesn't account for our `sampledImageDepthSampleCounts =
   1` scoping decision. Implement per-sample `OpImageFetch` for depth images, or
   find a way to get CTS's own narrower check to catch it as `NotSupported`.
6. **(6 cases, carried over)** `fragdepth`'s combined-depth-stencil-format bug:
   `{line,point,triangle}_list_{d24_unorm_s8_uint,d32_sfloat_s8_uint}_no_depth_clamp`
   -- a covered pixel's depth reads back as `0` instead of its real shaded
   value. Likely a `readDepth`/`writeDepth` packed-format addressing bug --
   start in `Executor.cpp`'s `readDepth`/`writeDepth` (~line 999).
7. **(carried over, several sessions running)** `L265`: residual
   `a2b10g10r10_snorm_pack32` ASTC-block-boundary alpha-decode bug in
   `ASTCDecode.cpp`.
8. **(carried over, overdue for many sessions)** `L228(e)`/`(f)`:
   broader-than-glsl/tessellation CTS sampling (`pipeline`'s other sub-suites,
   `api`, `synchronization`) at real scale -- still not done. Given this
   session's cascading-false-failure finding, any future broad sweep should be
   run in small per-group batches from the start, not one giant combined
   process.
9. **(low priority, confirmed unchanged for many sessions)**
   `offload-test-suite`'s own `spec_const_32_bits.test`/`WaveActiveMax.test`
   (failing) and `array_of_matrices.test` (stale `XFAIL:`) -- unrelated to
   FeMe/LLVM, need upstream lit-annotation fixes.
