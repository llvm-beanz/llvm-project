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

Can you close out L79 from the roadmap or other prerequisites blocking the
L-series milestones?

> **Vertex-attribute fetch reads past a bound attribute format's own declared
> channel count when a shader declares a wider input type**, discovered by L78's
> own investigation once its fix let real, non-zero forwarded vertex position
> data reach a hull/domain-stage test for the first time:
> `HullSystemValues.test`'s real vertex shader declares `float4 position :
> POSITION` as its input even though the bound `VertexData` attribute only
> supplies 2 floats (`Format: Float32, Channels: 2, Stride: 8`); the observed
> hull-stage-forwarded position value (`(-0.9, -0.9, -0.1, -0.9)`) exactly
> matches `[CP0.x, CP0.y, CP1.x, CP1.y]` -- i.e. the fetch reads 4 floats
> unconditionally from a buffer that only has 2 floats per vertex, spilling into
> the *next* vertex's data, rather than defaulting the missing components per
> the standard HLSL/Vulkan convention (0 for missing X/Y/Z, 1 for a missing W).
> Root cause: `Executor.cpp`'s `attributeFetchLayout(cpu::ResourceFormat)` maps
> `R32_FLOAT`/`R32G32_FLOAT`/`R32G32B32_FLOAT`/`R32G32B32A32_FLOAT` all to the
> identical `{FetchByteSize=4, ComponentsPerFetch=1}` -- the format's own real
> channel count (e.g. 2 for `R32G32_FLOAT`) is never tracked or used to cap how
> many components get decoded; the fetch loop's `InBoundsComponents =
> min(Elt.ComponentCount, AvailableFetches * ComponentsPerFetch)` is capped only
> by (a) the *shader's* own declared component count and (b) how many
> format-sized fetches fit in the *entire remaining buffer* (spanning subsequent
> vertices), never by the bound attribute's own declared width. Confirmed
> empirically via a scratch (non-production) YAML providing full
> float4/16-byte-stride vertex data in place of the real float2/8-byte-stride:
> the render target changes from entirely blank to fully rendered with no other
> change, isolating this as the sole remaining blocker for L78's own two named
> repros. Needs a real fix threading the bound attribute format's own channel
> count through `attributeFetchLayout`/the fetch loop, capping
> `InBoundsComponents` by it in addition to the existing checks, and defaulting
> any shader-declared components beyond it to 0 (X/Y/Z) or 1 (W) per standard
> convention -- with care for both float and integer component types -- plus its
> own new unit tests covering a shader declaring more components than its bound
> attribute format supplies, in each of the "missing W only" and "missing
> multiple trailing components" shapes. Not yet started.
