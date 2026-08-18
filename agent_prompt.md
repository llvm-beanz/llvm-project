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

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you complete the implementation V3 from the roadmap document?

> Push constants onto FeMe root constants, uniform buffers, binary and timeline
> semaphores, secondary command buffers, events, query pools

The previous agent left the note:

> This is the one item I did not fully close, and I want to be explicit about
> why rather than quietly shipping a half-working shader path. A Vulkan
> uniform buffer maps to SPIR-V's `Uniform` storage class, and real UBO access
> looks nothing like `StorageBuffer`'s existing bound-resource model: a
> storage buffer wraps one *homogeneous, dynamically-indexed* runtime array
> (`getpointer(handle, index)` then load/store the whole element, optionally
> GEP'd further into that one element's own fields), which is exactly what
> `feme::cpu::SPIRVResourceLoweringPass` already normalizes. A uniform block
> is instead one *fixed* set of differently-typed named fields at fixed byte
> offsets -- much closer in shape to the push-constant fix I'd just built
> (constant-offset GEP + load) than to storage buffers' indexed-array model,
> except it also needs a (descriptor set, binding) heap identity storage
> buffers get from their handle and push constants (a single global, no
> identity needed) do not. Neither existing mechanism fits it directly, and
> building a correct new one (global-with-binding-identity conversion pattern,
> a new access-chain pattern, resource-lowering-pass integration, careful
> handling of arrayed UBOs) is realistically its own roadmap-sized piece of
> work, comparable to R26's own scope for storage buffers.
>
> Given that, I made the same kind of pragmatic, explicitly-documented scope
> decision this codebase's own roadmap uses repeatedly (e.g. V2's storage-buffer
> object model landing separately from, but depending on, R26's shader-compiler
> side): I implemented the Vulkan object model in full --
> `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`/`_DYNAMIC` sharing storage buffers' pool/
> set/dynamic-offset accounting, producing a read-only `FemeDescriptor` -- and
> left the SPIR-V shader-side lowering as a documented, separately-scoped
> follow-up (Descriptor.h's file comment, and the Descriptor Model table's
> status column in FeMeVulkanDesign.md). This is real, tested, valuable work on
> its own (the runtime is fully ready the moment shader-side support lands),
> not a stub -- but it is honestly incomplete relative to the milestone's own
> one-line summary, and I said so in both the design doc and the roadmap
> rather than letting the roadmap's own accounting overstate what's done.
