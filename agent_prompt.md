---
model: claude-sonnet-5
resume: 6e011932-a46a-43ae-97b3-283c96c999ff
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

1. **(highest value, pick this up first)** Finish tracing `L150`'s
   offset bug to its exact origin: dump the IR *before* `Normalize.run()`
   in `Pipeline.cpp` (i.e. straight out of MLIR SPIR-V-to-LLVM
   translation, before *any* FeMe pass touches it) and check whether the
   4/20 offsets are already present there. If yes, the bug is in
   `SPIRVToLLVMPatterns.cpp`'s composite/function-call-argument
   lowering (start there, cross-reference against the `L124`
   `getTightMatrixType`/`getMatrixWholeAccess` family for the pattern of
   how a similar bug was fixed there, but expect this to be a *different*
   code path -- local/function-argument marshaling, not memory-block
   access chains). If the bad offsets are *not* yet present pre-SROA,
   the bug is upstream in LLVM's `SROAPass` itself for this exact
   `<3 x float>`-in-struct slicing pattern -- shrink the standalone `.ll`
   repro already used this session (`store <3xfloat>` at 0/12, read back
   at 4/20) down further and consider whether it's worth an upstream LLVM
   report.
2. Once traced, implement and test the fix following the `L147`
   `OffsetStructMemberReorderAccessChainPattern` fix as a template: a
   localized pattern fix plus a minimal reduced lit test, not a broad
   rewrite.
3. **`L147`'s remaining `ubo.*` sub-clusters** once `single_basic_type`
   is actually fixed: `random` (134), `2_level_array` (86),
   `single_basic_array` (81), `3_level_array` (59),
   `multi_nested_struct` (50), `instance_array_basic_type` (46),
   `single_struct` (33), `single_nested_struct_array` (31),
   `multi_basic_types` (27), `single_nested_struct` (16),
   `single_struct_array` (12), `link_by_binding` (1) -- worth checking
   whether any of these also hit the same `matNx3` signature once it's
   fixed, before assuming they're independent bugs.
4. `binding_model.shader_access` (11,834 cases, the overwhelming
   majority of `L147`) is still the eventual big one, likely wants its
   own dedicated session given the scale.
5. **`L148`** (14-case `subgroups.ballot_broadcast.*.
   requiredsubgroupsize{64,128}` hang cluster) is still untouched -- a
   hang, not a crash, expect to need a debugger or verbose logging.
6. **`L125(m)`/`L125(n)`** (upstream MLIR+LLVM `ConstOffsets` plumbing)
   -- still the largest not-yet-started cross-repo item, if a session
   wants a change of pace from CTS triage.
7. No scratch left over this session -- everything under `/tmp` from
   this session's investigation has been deleted, and the VK-GL-CTS
   checkout used for the L149 fix attempt is back to a clean `git
   status`.
