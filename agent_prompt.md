---
model: claude-sonnet-5
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
roadmap document (lettered with lowercase letters such as R34a or R34b) to break
down the remaining work for that milestone.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you implement roadmap milestone F8a and close out F8?

>  **F8's own remaining half: shader-side `subpassInput` local-read
>  consumption.** Needs, in order: (1) `OpTypeImage` with `Dim=SubpassData` and
>  `OpImageRead`'s subpass-local form modeled by the `spirv` dialect if it is
>  not already (unconfirmed as of F8 -- audit first); (2) a `SPIRVToLLVM`
>  conversion pattern reading directly from the currently-bound
>  dynamic-rendering color/depth/stencil attachment
>  (`feme::vulkan::RenderTargetBinding`, not a descriptor-set image) named by
>  the shader's own `InputAttachmentIndex` decoration, resolved through
>  `vkCmdSetRenderingInputAttachmentIndices`' mapping
>  (`GraphicsState::ColorAttachmentInputIndices`/`DepthInputAttachmentIndex`/`StencilInputAttachmentIndex`,
>  F8's own row) or, when that command was never called, the identity default
>  the same row's man-page citation describes; (3) once real local reads produce
>  correct pixels for a CTS-shaped test,
>  `dynamicRenderingLocalRead`/`VK_KHR_dynamic_rendering_local_read` can finally
>  be advertised (`EntryPoints.cpp`/`PhysicalDeviceInfo.cpp`, mirroring every
>  other F-row's dedicated-struct-agrees-with-aggregate precedent); (4)
>  `dynamicRenderingLocalReadDepthStencilAttachments`/`dynamicRenderingLocalReadMultisampledAttachments`
>  need their own depth/stencil- and multisample-attachment local-read cases
>  demonstrated (not just the plain color case (1)-(3) cover) before either
>  limit flips to `VK_TRUE`
