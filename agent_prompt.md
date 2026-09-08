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

Can you close out L72 from the roadmap or other prerequisites blocking the
L-series milestones?

> **A real, broad compute-stage `texture_functions` CTS re-run (1,375 cases,
> every `*_compute` case in that group) now shows genuine, substantial
> Pass-count movement for the first time (153 Pass, up from 0) following roadmap
> L70/L71's own fixes**, but the remaining 890 `Fail` cases break down into
> several distinct, already-partially-known, unstarted-here gaps rather than one
> single cause: (1) 284 cases still need `SPV_KHR_compute_shader_derivatives`
> SPIR-V-import support -- already tracked as roadmap L7, unaffected by this
> row; (2) 100 cases fail SPIR-V-to-LLVM legalization of `spirv.ImageFetch`; (3)
> 18 cases fail legalization of `spirv.ImageSampleDrefExplicitLod`; and (4) 340
> cases (split across four distinct "unhandled opcode" numbers -- 92, 94, 103,
> and 106/107 -- reported by the SPIR-V importer) remain entirely untriaged --
> not yet identified against the SPIR-V spec's own opcode table, let alone
> reduced to real IR. None of these four buckets have had their own real IR
> reduction yet; per this project's own established precedent, each should get
> one before being scoped as its own row (or rows), rather than attempting a fix
> blind. Not yet started.
