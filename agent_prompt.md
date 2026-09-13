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

Can you work on H101m or other blocking work to make progress on the H-series
milestones?

> **`transform_feedback.fuzz.*instance_array*`'s remaining 61 (of the original
> 68) `VK_ERROR_INITIALIZATION_FAILED` pipeline-creation failures, confirmed
> distinct from and unaffected by H101k's own leading-pad fix**: breaks down
> into (at least) four distinct symptoms by their own
> `mlir-translate`/`feme-opt` diagnostic, none sharing H101k's own
> leading-pad-before-a-*single*-real-member shape: (1) 28 cases hit
> `'llvm.mlir.constant' op attribute and type have different integer types:
> 'si32' vs. 'i32'` -- an `si32`-vs-`i32` signedness mismatch somewhere in
> constant-attribute construction, likely for a signed-integer stage-IO member
> (roadmap H101n: this attribute-type bug itself is now fixed, letting these
> cases progress further -- some now hit H101o's own newly-filed corruption
> instead); (2) ~14 cases still hit `failed to legalize operation
> 'spirv.GlobalVariable'` for a block with two or more *genuinely distinct* real
> members (unlike H101k's single-real-member-plus-pad shape), several combining
> a matrix/vector member with a *nested single-member struct* member (e.g.
> `!spirv.struct<(vector<4xf32> [RelaxedPrecision])>` as one member of an outer
> multi-member block) -- a shape H101i's own "tight vector" retry may not extend
> to; (3) 3 cases hit `feme-graphics-validate-stage: ... unresolved stage-IO
> global-variable access to 'spirv_var_N'` (the rewrite genuinely not
> recognizing some other, not-yet-identified shape, rather than H101k's own
> now-fixed leading-pad-count-confusion mechanism); (4) 1 case hits
> `'feme.stage.output.store' ... row 18 is out of range for element 1`, an
> out-of-bounds row distinct from every other symptom here. Not yet triaged --
> needs per-symptom-family standalone `feme-translate`/scratch-unit-test repros
> (mirroring H101a's/H101k's own technique) to identify each of the (at least)
> four distinct root causes, starting with the largest (28-case) `si32`/`i32`
> group (roadmap H101n: this group's own attribute-type bug is now fixed;
> re-triage this bucket's own counts against the current binary before
> continuing)
