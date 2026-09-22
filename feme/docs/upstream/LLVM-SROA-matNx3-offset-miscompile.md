# Retracted: `L150`'s `matNx3` offset bug was FeMe-internal, not upstream LLVM

**Status**: retracted. This draft's original claim -- that `L150`/`L147`'s
`ubo.single_basic_type.*.matNx3.*` failure cluster was caused by an
upstream LLVM `SROAPass` miscompile -- was itself mistaken, and this file
is kept only so a future session that finds it (e.g. via search) does not
re-derive the same, now-corrected, investigation from scratch. **Nothing
here should be filed against `llvm/llvm-project`.**

## What actually happened

A later session (see `feme/docs/Roadmap.md`'s `L150` row and
`feme/docs/VulkanCTSReport.md`'s "L150 follow-up" section for the full,
current writeup) traced the real root cause to
`feme::graphics::UnrollConstantTripCountStageLoopsPass`
(`feme/lib/Transforms/Graphics/UnrollConstantTripCountLoops.cpp`): that
pass runs its own internal `SROAPass`+`InstCombinePass` on every Vertex/
Fragment entry point *before* `Pipeline.cpp` ever substitutes the
module's real host `DataLayout` for `importShaderModule`'s own
placeholder one (a placeholder deliberately kept in place that long for
`CanonicalizeStagePass`'s sake, per that file's own roadmap-H82 comment).
That placeholder datalayout has no explicit vector-alignment
specification, so `InstCombine` folds a `[K x <3 x float>]` matrix
temporary's per-column `getelementptr` into a *tightly-packed* (12-byte)
literal byte offset instead of the *padded* (16-byte) one every real
target -- and std140/std430's own vec3-padding rule -- actually uses for
this shape. Once baked in as a plain integer literal, that offset can
never self-correct once the real `DataLayout` is substituted in later.

This is why every earlier attempt in this file's own original
investigation to reproduce the bug via the `opt` CLI on an extracted IR
snapshot failed: each attempt (reasonably, but as it turns out
incorrectly) substituted a *real* target's datalayout string into the
extracted file so `opt`/`lli` would accept it at all -- silently curing
the exact bug being chased in the process, since the real target's
datalayout (like the fix's own default-constructed one) already agrees
with std140/std430 on this shape.

**Fixed** entirely within FeMe:
`UnrollConstantTripCountStageLoopsPass::run` now temporarily substitutes
a plain, default-constructed `DataLayout` (LLVM's own built-in rules,
which already agree with every real target on this shape) for the
duration of its internal `FunctionPassManager` run, then restores the
module's original `DataLayout` immediately afterward -- mirroring the
same temporary-substitute-then-restore idiom `CompiledStage.cpp`'s
`getGroupSharedRequirements` call already uses for an analogous reason.
Covered by a new unit test,
`feme/unittests/Transforms/Graphics/UnrollConstantTripCountLoopsTest.cpp`.

No upstream LLVM change is needed, and none was ever filed.
