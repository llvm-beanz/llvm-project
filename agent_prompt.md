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

Can you close out L71 from the roadmap or other prerequisites blocking the
L-series milestones?

> **Every compute-stage entry point using the common GLSL/HLSL early-return
> bounds-check idiom (`if (gid.x >= size.x \|\| gid.y >= size.y) return;`) fails
> outright at pipeline creation** with `feme-cpu-linearize: function '<name>':
> divergent branch in '<bb>' has no reconvergence point`, discovered by roadmap
> L70's own closing re-run once its resource-normalization fix let real
> compute-stage sampling shaders reach the linearizer for the first time. Root
> cause (per `VerifyStructured.cpp`'s `checkDivergentBranchesReconverge`, also
> checked by `Linearize.cpp` itself): a non-uniform (divergent, i.e.
> per-invocation-varying) conditional branch's immediate post-dominator does not
> exist in the function's `PostDominatorTree` -- the classic shape of "one arm
> of the branch never returns to a common point" that an early `return` inside a
> divergent `if` produces, since control flow from that arm simply exits the
> function instead of rejoining the other arm anywhere. Unlike a fragment shader
> (where feme's own `SIMDize`/`Linearize` machinery already has an established
> masked/helper-invocation lane model for exactly this kind of partial-lane
> exit), a compute shader's divergent early return currently has no equivalent
> handling anywhere in the linearizer at all -- confirmed via grep, no branch or
> comment in `Linearize.cpp` mentions early-return masking for any stage. This
> is a large, genuinely unstarted linearizer/control-flow feature (mapping a
> divergent early return onto a masked/predicated continuation instead of
> rejecting the branch outright), not a small follow-on fix, and is very likely
> the single highest-value remaining blocker for turning any of this project's
> already-landed compute-stage sampling/derivative fixes
> (L60/L63/L65/L66(h)/L66(i)/L66(j)/L66(k)/L69/L69(a)/L70) into real CTS
> Pass-count movement, since this exact idiom is pervasive in real compute
> shaders. Not yet started; needs its own real design investigation into how a
> divergent early return could be lowered to a masked/predicated form compatible
> with this target's existing structured-control-flow linearizer, likely
> followed by its own further breakdown into smaller rows once a design is
> chosen, per this project's own established splitting precedent for large gaps.
