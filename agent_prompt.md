---
model: claude-sonnet-5
resume: 3e3ed1ca-e8e0-43ee-a165-5cdf3bba2524
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file.

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

# Request

Can you work on H101t or other blocking work to make progress on the H-series
milestones?

> **`transform_feedback.fuzz.all_unordered_and_instance_array.2`'s
> `PromoteMemToReg`/`isAllocaPromotable` assertion crash inside
> `CanonicalizeStagePass`** (newly exposed by H101s's own closing fix, which let
> this case's `spirv.GlobalVariable` legalize for the first time -- confirmed
> via a `gdb` backtrace that the crash is not a JIT-compiled-code fault but a
> compile-time assertion in `CanonicalizeStage.cpp`'s own final
> `PromoteMemToReg(Allocas, DT)` call, reached from `canonicalizeSPIRVStage` for
> both `random_vertex` and `random_geometry` variants): `deqp-vk:
> .../PromoteMemoryToRegister.cpp:816: ... Assertion \`isAllocaPromotable(AI) &&
> "Cannot promote non-promotable alloca!"\` failed`, meaning one of
> `ShadowValues`' own synthesized read-modify-write allocas ends this pass's
> rewrite with a use `isAllocaPromotable` does not accept (not a plain
> load/store) -- almost certainly because some access into this case's own
> newly-legalized, genuinely multi-member nested-struct block member (H101s's
> own `.2` repro shape: a nested struct with a `mat3x3` and a `vector<4xsi32>`,
> not just one member) never gets rewritten into a `feme.stage.*` call at all,
> since `CanonicalizeStage.cpp`'s row/component-shape derivation and per-member
> decoration lookup (`TakeBlockPath`, `getStageIORowShape`,
> `peelSingleMemberStruct`) only know how to peel through a *single*-member
> nested struct, not treat each of a *multi*-member nested struct's own real
> members as its own independently-addressable, independently-decorated stage-IO
> element the way the outer block's own top-level members already are. Not yet
> triaged -- needs a standalone `feme-opt --feme-canonicalize-stage` repro
> (mirroring this milestone's own repeated technique) of `.2`'s exact shape to
> find which specific access survives unconverted and confirm whether the fix
> belongs in `TakeBlockPath`'s own per-member loop (extended to recurse into a
> multi-member nested struct member, assigning each of its own real members a
> `Location`/`ElementID` the same way an ordinary top-level member gets one) or
> in a new, more targeted "nested struct member is itself a stage-IO-decorated
> block" helper
