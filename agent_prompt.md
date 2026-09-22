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

**When you find an issue outside FeMe**: Create an isolated reproducer, fix it,
and apply the fix in a commit that only touches files from outside the FeMe
subdirectory. Ensure that fixes to other LLVM sub-projects are self-contained
and tested.

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

# Request

Can you please work on the FeMe ICD implementation? The previous session gave
the next steps:

1. **(dedicated session, ~half a day)** Implement the `L148` fix exactly as
   scoped above: new `llvm.spv.wave.broadcast` intrinsic
   (`IntrinsicsSPIRV.td`) -> `BroadcastConversionPattern` emits it ->
   new `WaveCallKind::Broadcast` (`WaveCalls.h/.cpp`, scalar `RetTy`) ->
   `SIMDize.cpp` dispatch (mirror the existing `isVectorOperandReduceKind`
   per-component-decompose shape for `vec2`/`vec3`/`vec4` cases) -> new
   `lowerBroadcast` in `WaveLowering.cpp` (one `extractelement` for the
   uniform index, one masked `extractelement`+`select`, no allocas, no
   loop). Test each translation phase per usual convention. Re-run the full
   14-case cluster plus a broader `subgroups.*` sweep before calling it
   done, since `Shuffle`/`Rotate`/`ReadLaneAt` share the machinery being
   touched and must not regress.
2. If step 1's fix doesn't fully resolve the hang, check the secondary,
   unconfirmed hypothesis noted in the `L148` row: `SROA::runSROA`'s own
   do-while loop may re-invoke `PromoteMemToReg` (and rebuild
   `PromoteMemoryToRegister.cpp`'s `LargeBlockInfo` from scratch) multiple
   times per function as post-promotion allocas trickle in. Not pursued
   this session since the `O(W^2)`-IR explanation looked sufficient on its
   own.
3. `binding_model.shader_access` (11,834 cases, `L147`'s big remaining
   cluster) is still untouched and still wants its own dedicated session
   given the scale.
4. `L125(m)`/`L125(n)` (upstream MLIR+LLVM `ConstOffsets` plumbing) is still
   the largest not-yet-started cross-repo item.
5. No scratch left over -- all `/tmp/l148_*` files deleted, debug
   instrumentation reverted, working tree clean before this commit.

