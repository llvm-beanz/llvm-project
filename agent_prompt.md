---
model: claude-sonnet-5
resume: b8ce7a07-6a6f-4205-864b-b09621346aaf
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

Can you please continue the work on the Vulkan FeMe ICD? A previous session
suggested the next steps:

1. **(~1 day, the real next step)** Implement fix option **(b)** above:
   in `collectUniformPassThroughRegion`, before calling `isDivergentBranch`
   on a candidate block's `CondBr`, also check whether the condition
   transitively depends on a resource/image load *inside cycle `C`*
   whose mask argument is currently constant `true` -- if so, predict
   whether this cycle will need a genuinely non-constant
   `active.live`/`active.sideeffect` phi (i.e., whether `HeaderDivergent`/
   `LatchDivergent`/any `OtherCondBrBlocks` member is divergent, meaning
   masking of this kind will actually happen), and if so, pessimistically
   reject this block from the pass-through region (`return std::nullopt`)
   so it falls through to proper divergent-diamond flattening instead.
   Validate on `select_iteration_count_{fragment,vertex}` across
   `do_while_dynamic`/`for_dynamic`/`while_dynamic` (the known 6 cases),
   then a full `dEQP-VK.glsl.loops.special.*` (552 cases) sweep to
   confirm zero regressions plus those 6 flipping if the normal
   diamond-flattening path can actually handle Header's shape once
   rejected -- it may not, which would need yet another follow-up.
2. **(standing)** `L419(g)` (tessellation residuals, 30 cases) --
   untouched, same IR-dump-and-reduce methodology candidate.
3. **(standing)** `L419(h)`'s remaining 16 cases (12 special-loop + 4
   loop-in-switch) -- distinct shapes, need their own reduction.
4. **(check-in, cheap, every session)** Keep checking
   `/tmp/l437_full_run2/` progress (`ps aux | grep deqp-vk`,
   `ls /tmp/l437_full_run2/qpa | wc -l`).
5. **(when the sweep finally completes)** Replace `VulkanCTSReport.md`'s
   whole master per-group table using the union of completed cases
   across both `/tmp/l437_full_run*` directories.
