---
model: claude-sonnet-5
resume: ed0156d8-7270-4d71-9053-e6cff6d7f6b5
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

1. **(30-60 min)** Add back *just* the `remapNestedStructMemberIndices`
   `ArrayType`-branch instrumentation from this session (an unconditional
   `llvm::errs()` print of whether `ElementType` is a non-power-of-2 vector,
   right where `ElementType = ArrayTy.getElementType();` is assigned) and
   rerun `nested_structs.2` alone, without any of the three fix attempts
   applied. If it fires with `elemIsVec=1`, the array-branch fix from this
   session is the right location and just needs another look at *why* it
   didn't change the outcome (maybe the inserted index is off by one, or a
   second, unrelated bug is stacked on top). If it still doesn't fire, this
   whole code path is a dead end and something else entirely handles this
   struct's real conversion -- worth checking `CompositeExtract`/
   `CompositeInsert` patterns next (`remapNestedStructMemberIndices`'s other
   two call sites, lines ~3404 and ~4590), and `SIMDize.cpp`/`GroupShared.cpp`
   for any independent flat-buffer address computation that might bypass all
   of this MLIR-level machinery for `Workgroup`-storage variables specifically.
2. **(unknown, follow-on)** Once the real path is confirmed, redo the
   `ArrayType`-branch fix (already drafted and reamovable from this session's
   summary above) *in isolation* first, verify `ninja check-feme` alone with
   just that one change (no stacking with the other two candidate fixes),
   and only add the other two back if each is independently confirmed to
   help.
3. **(10 min)** Investigate why stacking the two earlier-session fixes (offset-
   branch tightening + `convertArrayTypeIgnoringDecorations`'s stride check)
   caused a 2-lane vector to get spuriously tight-vector-wrapped in
   `spirv-to-llvm-array-of-matrix-struct-member.mlir`'s neighborhood -- that
   regression needs root-causing on its own before either of those two fixes
   is reapplied, even if L207's real bug turns out to need one of them too.
4. Carried over, untouched again this session (all from at least two
   sessions ago): L201(d) (mesh/tessellation f16 I/O, 120 cases, needs
   `--deqp-log-images=enable` pixel diff), L201(b)/(c) (27+25 cases, reduced
   to symptoms but not started), `input_output_float_32_to_16`'s own 100
   `_rtz` failures (still just noted, never checked against existing
   tracking), the overdue full/broad CTS re-run (last full run: 2026-09-26),
   and `check-hlsl-feme-vk` against the `offload-test-suite` `feme` branch
   (still never run).
5. `stash@{0}` ("full remaining changes on top of commit1") is still sitting
   untouched from an earlier session -- still worth a deliberate look
   (`git stash show -p stash@{0}`) or an explicit `git stash drop` next time
   L197 or whatever it covers comes up, rather than leaving it indefinitely.
