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

1. **(carried over, unresolved this session, likely a few hours)** `fragdepth`
   (18 cases), two separate sub-problems, pick whichever fits the next session's
   time budget:
   - the 12-case non-multisample value bug (`Mismatch at pixel (10,2,0):
     expected 0 but got -0.164062` on `line_list_d32_sfloat`) -- start here,
     it's a real correctness bug, not a scoping decision. Use
     `--deqp-log-images=enable` to see the full rendered-vs-reference depth
     image, not just the first mismatch, to tell whether it's a wrong clear
     value or a rasterization-coverage leak.
   - the 6-case multisample image-creation gap needs a real feature decision
     (implement per-sample-indexed `OpImageFetch` for depth images to honor
     CTS's expectation, or decide it's out of scope and get the CTS's own
     `checkSupport` to catch it some other way) -- larger, lower priority than
     the value bug.
2. **(30 min, check first)** Re-run the full `dEQP-VK.glsl.*` sweep from scratch
   early next session (remember the cwd gotcha above) -- this session's sweep
   only got to 6,940/28,420 cases (~120/min, unusually slow, host-load-dependent
   and not investigated) before being stopped; no reliable full residual tally
   exists past `L278`'s 89-case baseline (now 86, since `pointcoord`'s 3
   cleared) until this re-runs to completion. If it's still this slow, consider
   capping with `--deqp-caselist-file` splits or accept a partial-sweep
   spot-check instead of a full one.
3. **(a few hours, largest untriaged cluster, unchanged rank for many
   sessions)** `loops` (30, likely `*_dynamic_iterations` per `L277`'s write-up
   -- two separate bugs already known to block non-leaf cycle linearization
   there) -- still the top pick by size if `fragdepth` isn't picked up first.
4. **(small, unexplored across several sessions)** `builtin` (14, may include
   the 8 new `cosh`/`sinh` precision fails this session's partial sweep found --
   not confirmed), `struct` (12) -- only became visible as separate clusters
   once bigger ones cleared.
5. **(small)** `demote` (9), `derivate` (3) -- tiny, likely quick once picked
   up.
6. **(carried over, several sessions running)** `L265`: residual
   `a2b10g10r10_snorm_pack32` ASTC-block-boundary alpha-decode bug in
   `ASTCDecode.cpp`.
7. **(carried over, overdue for many sessions)** `L228(e)`/`(f)`:
   broader-than-glsl/tessellation CTS sampling (`pipeline`'s other sub-suites,
   `api`, `synchronization`) at real scale -- still not done.
8. **(low priority, confirmed unchanged for many sessions)**
   `offload-test-suite`'s own `spec_const_32_bits.test`/`WaveActiveMax.test`
   (failing) and `array_of_matrices.test` (stale `XFAIL:`) -- unrelated to
   FeMe/LLVM, need upstream lit-annotation fixes. Did not re-verify this session
   (skipped `check-hlsl-feme-vk`, see above).
