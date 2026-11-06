---
model: claude-sonnet-5.5
resume: 867b2fbf-2238-40d8-8a30-ea8036208e37
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

Please continue working on the FeMe Vulkan ICD. Suggested next steps:

1. **Check the sweep first.** `ls -la
   /home/dev/dev/VK-GL-CTS/run/feme-20261106-full-sweep/report.txt`.
   - If present: read it, diff against the stale 105-failure list, file
     any *new* regressions as their own roadmap rows before triaging
     anything else.
   - If absent and no `deqp-vk`/`run_vulkan_cts.py` processes are running
     (`ps aux | grep deqp`): it died mid-run. Follow
     `.instructions.md`'s "no `--resume` flag" recipe -- union every
     completed `CasePath` out of all `qpa/*.qpa` files, subtract from
     `all-cases.txt`, launch a fresh detached sweep (new `--output-dir`)
     against only the remainder.
   - Either way, first compare the sweep's `deqp-vk` process start time
     to `libfeme_vulkan.so`'s mtime -- if the `.so` is newer (it isn't,
     as of this commit, but will be the moment any future session
     rebuilds it), the in-flight run is stale; kill and relaunch.
2. `ENV6(c)(5a)` real fix (flatten()-level, not validate()-level): needs
   a mask-merging phi at the shared tail block (`Flow2` in the repro)
   that combines the header-exit-relay path's own live/sideeffect value
   with the body-diamond's per-arm merge selects, instead of reusing the
   diamond's own selects unconditionally for every predecessor. This is
   risky, core-linearizer surgery -- give it a dedicated session with
   `DiamondFlattener`/`LoopLinearizer` unit tests, not a squeezed-in
   attempt. Repro still at `/tmp/nl.ll` (regenerate from
   `/tmp/pl-cov-function-infinite-loop-return-identical-condition.txt`
   if lost) and the pinned-diagnostic lit test
   `env6c5-outer-exit-after-inner-loop-unsupported.ll`.
3. Check `cosh-return-inf-unused` -- its error is the *different*
   "uniform pass-through region" diagnostic (~`Linearize.cpp` L9116),
   likely not the same shape as `ENV6(c)(5a)`, not yet re-examined.
4. `ENV6(c)(5)`-`(7)` graphicsfuzz (~90 cases, big `Linearize`/`SIMDize`
   cluster): needs its own dedicated multi-session slot, unchanged.
5. `ENV6(d)(5b)`, `ENV5(c)(7)(b)` (hull lockstep barrier), offload
   upstreaming: all still blocked/lower-priority, unchanged this
   session.
