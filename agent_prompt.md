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

Can you work on L66(k) or other prerequisites blocking the L-series milestones?

> **`Plain1D`/`Array1D` depth-comparison (shadow) sampling rejects a real,
> nonzero `ConstOffset` outright** (`sampler1d{,array}shadow_{bias,}fragment`
> under every wrap mode of `textureoffsetclamp`/`texturegradoffsetclamp`, 20
> real CTS cases total, discovered by L66(j)'s own four-group
> `shaderResourceMinLod` re-measurement) -- `SPIRVResourceLowering.cpp`'s
> `isSupportedOffset` already special-cases a bare scalar `i32` `ConstOffset`
> for the *ordinary* (non-`Dref`) `Plain1D`/`Array1D` sample path (roadmap
> L66(d)), but the depth-comparison path deliberately never passes
> `AllowPlain1DArray1D=true`, so it still falls through to `isZeroOffset`'s
> always-zero requirement for these two shapes -- needs its own real IR
> reduction of one of these exact cases to confirm the same bare-scalar-`i32`
> `ConstOffset` shape applies here too (mirroring L66(d)'s own precedent for the
> non-`Dref` path), then widening `isSupportedOffset`'s `Dref`-path gate plus
> `createSampleCmp1D`/`createSampleCmpArray1D` (neither of which threads an
> offset parameter at all today) and their
> `femeCpuImageSampleCmp1DV4F32`/`femeCpuImageSampleCmpArray1DV4F32` runtime
> counterparts to actually apply it.
