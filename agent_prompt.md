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

Can you close out L80 from the roadmap or other prerequisites blocking the
L-series milestones?

> **`HullSystemValues.test` (L77/L78/L79's own named repro) still fails its
> `SystemValues` result check even after L79's vertex-attribute-fetch fix**: the
> `ResultBuffer` is no longer all-zero -- `SV_PrimitiveID`,
> `SV_OutputControlPointID`, and `SV_TessFactor`/`SV_InsideTessFactor` all now
> round-trip correctly -- but the smuggled per-control-point `position` data
> (buffer elements 0/1 and 7/8, one pair per patch) still mismatches: expected
> `(0, 0)`/`(1, 1)` but observed `(-0.9, -0.9)`/`(0.1, 0.1)` (confirmed via a
> real `offloader` re-run of this exact repro after L79's fix). Unrelated to
> L79's own vertex-attribute-format-channel-count scope (that fix is confirmed
> correct via its own passing unit tests and this row's own now-different
> symptom); a further, distinct gap somewhere in the hull-stage
> output-forwarding, domain-stage input-forwarding, or pixel-stage
> attribute-linking chain for this specific user-data field. Needs its own real
> IR reduction (the same technique this project's
> H6-series/H8-series/H9-series/L-series chains have used throughout), likely
> starting with a runtime-`printf`-instrumented JIT re-run of the hull and
> domain stage wrappers (mirroring L78's own successful technique) to isolate
> which stage's real output for this specific field is going unwritten or
> overwritten. Not yet started.
