---
model: claude-sonnet-5
resume: 52e661a0-b284-45ee-892f-3073721ca338
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

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

# Request

Can you continue the work on feme? The last agent's suggested next steps are:

## Next steps (start here)

1. **(~2-3 hours)** Fix `getMatrixWholeAccess`'s non-wrapper branch to
   walk through zero-or-more intervening struct-member selections
   before the final matrix-member select (mirroring how
   `peelInstanceArrayPointer` already peels array nesting for the same
   function) -- track the innermost struct + member index actually
   reached, not just the outermost block struct's own direct member.
2. **(~2-3 hours, do together with #1, not separately)** Extend
   `getTightNestedStructType`/`getTightMatrixType` to *widen* (not just
   tighten) a nested struct's own matrix member to its declared
   `MatrixStride`, using the same substitution `getPhysicalMatrixMemberType`
   already builds for a direct block member. Verify against the
   isolated repro (recreate `/tmp/mat2_stride16.mlir`'s shape as a
   permanent lit test) before touching the CTS sweep.
3. **Always, before declaring any fix done**: re-run the *full*
   `ssbo.*` sweep (not just `random`), not only the subset the fix
   targets -- this is what caught this session's regression.
4. Re-run `dEQP-VK.compute.pipeline.builtin_var.*` (L106's own vec3
   regression coverage) as a sanity check, since this area is adjacent.
5. `random`'s other symptom buckets (25 "Result comparison and counter
   values are incorrect", 15 "Counter value incorrect",
   1 `VK_ERROR_INITIALIZATION_FAILED`) are still un-triaged past this
   session's own `.39`/`.41` repros -- `.39`'s own root cause (a
   "Counter value incorrect" case) is still open; do not assume it
   shares `.41`'s matrix-nesting bug without its own trace.
6. `L124(a)/(b)/(c)/(d)/L125/L126/L116(f)` all remain untouched, standing
   fallbacks from prior sessions.

## State for next session

- Working tree clean, HEAD at `b8d8983c3639` (2 doc-only commits this
  session, no code commits -- see "why the wrong turn happened" above).
- `ninja check-feme`: 3,208/3,211 Passed, 3 Unsupported, 0 Failed
  (unchanged).
- `ssbo.*`: **3,187 Pass / 55 Fail / 8,983 NotSupported** (of 12,225) --
  unchanged from last session.
- `compute.*`: 679 Pass / 6 Fail / 60,775 NotSupported (unchanged).
- `/tmp` scratch cleaned up (this session's own; see below).
