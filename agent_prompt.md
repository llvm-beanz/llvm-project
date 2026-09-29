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

1. **(unknown size, ready to pick up, largest known residual)** `L268`:
   root-cause the `uvec3`-only `"feme.tight_vector"` MLIR insertvalue
   type-mismatch bug (18 CTS cases across
   `uaddcarry`/`usubborrow`/`umulextended`/`imulextended`). Start by
   grepping `feme/lib/Conversion/SPIRVToLLVM/` for `"feme.tight_vector"`
   or `tight_vector` to find the type-converter code emitting this
   struct layout for 3-component vectors, then figure out why it
   disagrees with the plain `vector<3xi32>` SPIR-V's own
   `IAddCarry`/`ISubBorrow`/`UMulExtended`/`SMulExtended` conversion
   pattern expects there. Not yet confirmed whether the responsible
   code is FeMe-authored (`SPIRVToLLVMPatterns.cpp`, inside `feme/`) or
   upstream MLIR -- that determines whether the standing "isolate
   issues outside FeMe into their own commit" instruction applies.
2. **(check first, may already be done)** The full `dEQP-VK.glsl.*`
   sweep (28,420 cases) this session kicked off in the background --
   check `/tmp/l266_full_sweep.txt`/`/tmp/l266_full_sweep.qpa` for
   completion; if the shell's gone, just relaunch it (same command,
   `mode="async"`, this convention is well-established over many
   sessions now). Compare the fresh tally against the last confirmed
   full-sweep baseline (`L263`'s post-fix: 18,634 Pass / 823 Fail /
   8,963 NotSupported) -- L266's fix should shave off roughly
   246 - 18 (residual `L268` fails) = ~228 Fail, so expect somewhere
   near 18,862 Pass / ~595 Fail / 8,963 NotSupported. Worth confirming
   this matches rather than assuming.
3. **(unknown, still filed, not started)** Remaining smaller
   `L258`/`L263`-era clusters (`atomic_operations` 96,
   `matrix.mul.dynamic` 24 + other `matrix.*` variants,
   `440.linkage.varying` 49, `shader_expect_assume.*` 48 combined,
   `loops.special.*_dynamic_iterations` 30 combined) -- worth a
   dedicated triage session after `L268`/`L267`.
4. **(carried over, unchanged, several sessions running)** `L267`:
   `texture_functions.query.*` cluster (144 cases: `texturequerylod`
   70, `texturequerylevels` 34, `imagesizems`/`texturesizems` 16 each,
   `texturesamples` 8) -- filed several sessions ago, not yet started.
5. **(carried over, unchanged, several sessions running)** `L265`:
   residual `a2b10g10r10_snorm_pack32` ASTC-block-boundary alpha-decode
   bug in `ASTCDecode.cpp`. Still not picked up.
6. **(carried over, unchanged)** `L228(e)`/`(f)`: broader-than-glsl/
   tessellation CTS sampling (`pipeline`'s other sub-suites, `api`,
   `synchronization`) at real scale -- still not done.
7. **(low priority, not a regression, unresolved for many sessions)**
   `offload-test-suite`'s own
   `Feature/SpecializationConstant/spec_const_32_bits.test`/
   `WaveOps/WaveActiveMax.test` (failing) and
   `Feature/PushConstant/array_of_matrices.test` (stale `XFAIL:`) still
   need upstream lit-annotation fixes -- unrelated to any FeMe/LLVM
   change, flagged across many sessions now.
8. No git stashes left open this session (none were used -- both repos
   were clean/in-sync throughout, confirmed via the mandatory session-
   start `vulkaninfo`/branch-drift checks).
