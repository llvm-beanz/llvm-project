---
model: claude-sonnet-5
resume: 50bf9c01-6e85-44df-8b7a-5c13ed0b05e1
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
letter deep going forward.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on H8p or other prerequisites blocking the H-series milestones?

> **The genuine integer-format `COLOR_ATTACHMENT_BIT` gap H8e split off.**
> `r16_{sint,uint}`, `r16g16_{sint,uint}`, `a2b10g10r10_uint_pack32`,
> `a8b8g8r8_{uint,sint}_pack32` (7 `VkFormat`s across 4 distinct
> `ResourceFormat` gaps once `A8B8G8R8_*_PACK32`'s existing `R8G8B8A8_*`
> aliasing is accounted for) are each missing only
> `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT`, confirmed by a real `deqp-vk` run --
> but H8e's own investigation found this is not a simple advertisement fix:
> `Executor.cpp`'s `executeDraws` hard-rejects any pipeline whose fragment
> output `ComponentType != SignatureComponentType::Float` outright (regardless
> of the target attachment's own format), so no `ivec4`/`uvec4` fragment-shader
> output can be created or drawn today. A real fix needs, at minimum: (1)
> widening that `executeDraws` check to accept an integer-typed fragment output
> when the bound attachment format is itself an integer format; (2) a new
> integer-reading counterpart to `readFragmentColor` (which today
> unconditionally calls `FSOutput.readFloat`); (3) confirming/extending
> `packClearColor`/`unpackColor` (`ImageFixture.cpp`) to actually pack/unpack
> these four `ResourceFormat`s (not yet checked); (4) a new `true` case in
> `RenderPass.cpp`'s `isSupportedColorAttachmentFormat` (today explicitly
> `false` for every integer format, per its own "an integer format no fragment
> output writes yet" comment) once (1)-(3) land, so the advertised bit is never
> dishonest. Needs its own scoping pass splitting this into sub-rows once the
> size of (1)-(3) is better understood, mirroring H8k's own precedent for
> BC6H/BC7
