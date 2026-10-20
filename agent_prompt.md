---
model: claude-sonnet-5
resume: 1b814ab3-f423-4724-ae98-f676112844de
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

**When you find an issue with environment or process**: Codify it in Feme's
.instructions.md file so future sessions don't lose it.

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

**Always**: Make sure you are not using precompiled headers in your build.

**Always**: End each session with a commit of the agent thoughts even if
mid-investigation to avoid losing documentation.

# Request

Please continue working on the FeMe Vulkan ICD. The previous session suggested
the next steps:

1. **(~20-30 min, do this first)** Direct instrumentation: patch a *scratch
   copy* of the compiled Mandelbrot `.o`/IR so the final store writes
   `Diverged`'s own value (or `%.wide2`, zero-extended) into the color channels
   instead of the computed palette `Color`, rebuild `libfeme_vulkan.so`, rerun,
   and inspect the output PNG for the 8 known-bad pixel coordinates from this
   session (`(3832-3839, 901)`). If it reads "diverged" (nonzero) there, the
   loop's own merge is fine and the bug is 100% in the outer dispatch blocks —
   skip straight to step 3. If it reads "not diverged" there, the merge itself
   is wrong at runtime despite looking right in the IR — go to step 2.
2. **(~30-60 min, only if step 1 says the merge itself is wrong)** Re-examine
   `freezeExitRelayValue`/the `HeaderExitRelayValues` restore loop for a subtler
   bug (e.g. ordering relative to `freezeLoopCarriedValues`'s own mutation, or a
   mask computed before vs. after some other CFG edit this session didn't check)
   — this would mean `L465`/`L466`'s own validation (which only checked IR
   *shape*, not actual runtime values against this specific real shader) missed
   something; add a new reduced `.ll` reproducer matching whatever's found.
3. **(~30-60 min, the more likely branch per two sessions' worth of flagging)**
   Trace the outer `if (Diverged)` dispatch
   (`Flow`/`live.t2`/`live.f4`/`.linearized` blocks) value-by-value in the
   post-SIMD dump, specifically looking for another `addIncoming`/phi-restore
   call elsewhere in `Linearize.cpp` (not `HeaderExitRelayValues`) that has the
   same "silently keeps a stale literal on a reconvergence edge instead of
   freezing" shape `L465`/`L466` already fixed twice. Grep for other
   `PN->addIncoming`/`setIncomingValue` call sites outside the code this session
   touched as a starting point.
4. **(~15-20 min once the real bug is found and fixed)** Re-validate:
   `check-feme`, the real `check-hlsl-clang-feme-vk` Mandelbrot test (confirm
   `PixelPercent` passes under 0.2%), all 6 `assertion-crash-cases.txt`
   reproducers. Strike `ENV3(c)(6)`/`ENV3(c)` itself in `Roadmap.md` if it fully
   closes; update `VulkanCTSReport.md`.
5. **(only if, against expectation, a future session finds the residual
   genuinely *is* partly inherent after all)** Loosen `offload-test-suite`'s
   `Basic/Mandelbrot.test` `rules.yaml` tolerance as a documented fallback — but
   this session's 8-for-8 sample makes that increasingly unlikely to be needed
   at all.
6. **(after `ENV3(c)` truly closes)** `ENV4`: full current CTS corpus collection
   — still needs a dedicated clean-host block, carried forward unchanged from
   every prior session.
7. **(after `ENV4`)** `ENV5`: partition the 103 SPIR-V instruction failures + 4
   graphicsfuzz failures into independently tested fixes.
8. **(low priority)** Upstream the 3 `offload-test-suite` patches to
   `llvm-beanz/offload-test-suite` if push access ever becomes available.
