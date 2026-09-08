---
model: claude-sonnet-5
resume: ec2f5570-263a-4b95-917f-6c2230e594cf
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
if it already exists, and commit it in its own commit when you're done.

# Request

Can you close out L72(d) from the roadmap or other prerequisites blocking the
L-series milestones?

> **76 `dEQP-VK.glsl.texture_functions.*_compute` CTS cases fail SPIR-V import
> outright with `"unhandled opcode <N>"` for three opcodes MLIR's own SPIR-V
> dialect has zero enum/Op-class coverage for at all** -- `OpImageQuerySizeLod`
> (103, 34 cases), `OpImageQueryLevels` (106, 34 cases, directly correlated with
> the CTS `query.texturequerylevels.*` group), and `OpImageQuerySamples` (107, 8
> cases) -- split out of roadmap L72(a) once that row's own feme-local
> pre-import-rewrite fix for the other two "unhandled opcode" opcodes (92/94)
> landed, confirmed via a real re-run of roadmap L72's own 1,375-case caselist
> to be the entire remaining "unhandled opcode" bucket. Unlike
> `OpImageSampleProjExplicitLod`/`OpImageSampleProjDrefExplicitLod` (L72(a)'s
> own fix), none of these three opcodes can be rewritten into an
> already-supported equivalent form purely at the SPIR-V binary level: each
> *queries* information (an image's own mip-level count, multisample-sample
> count, or size at an explicit, possibly non-zero mip level) that no other
> already-supported opcode's return value can substitute for -- this is a
> genuine new runtime-capability gap, not just an import-time rewrite, likely
> needing (1) a real design investigation into whether feme's own
> resource-heap/`ResourceInfo.cpp` machinery already tracks an image's mip-level
> count and per-resource sample count anywhere (e.g. for `vkCreateImageView`'s
> own validation) that a new runtime query could reuse, and (2) either upstream
> MLIR TableGen op definitions for these three opcodes or a feme-local
> synthetic-opcode encoding, mirroring L72(a)'s own scoping-decision precedent.
> Not yet started; needs its own design investigation before implementation can
> begin.
