# `feme-render` — FeMe software graphics executor runner

## SYNOPSIS

```shell
feme-render [options] <scene .yaml file>
```

## DESCRIPTION

`feme-render` renders a textual scene description through FeMe's software
graphics executor (`FeMeGraphics`, see
[../FeMeGraphicsDesign.md](../FeMeGraphicsDesign.md)) and prints the resulting
attachments as textual image fixtures. It is the graphics counterpart of
[`feme-run`](feme-run.md): where `feme-run` turns "does this translate
correctly?" into "does this compute the right answer?" for a compute shader,
`feme-render` does the same for a vertex/fragment pipeline, so a `lit` test
can assert on rasterized coverage, interpolation, depth, stencil and blending
rather than only on the shape of the IR.

**Status (roadmap R32, "Basic triangle pipeline"):** this tool implements
scene parsing, attachment building/clearing, pipeline compilation (a scene's
`pipeline.vertex`/`pipeline.fragment` compile into a real `GraphicsPipeline`
description), and draw execution: a non-empty `draws` list runs through
`feme::graphics::executeDraws` against one color attachment, one
viewport/scissor, no depth/stencil, no multisampling, and
`blend: replace` -- other combinations are a diagnosed error rather than a
silent approximation (see "Draw flow" in
[../FeMeGraphicsDesign.md](../FeMeGraphicsDesign.md) for the executor's own
scope notes). `--workers`/`--tile-order`/`--reference` remain accepted for
forward compatibility only: the executor is a deterministic single-threaded
scalar implementation today, so every value of each produces identical
output, but true parallel tiling and a differential scalar-reference path
are later scheduling optimizations, not part of this milestone. Shader
modules are loaded as plain, already-raised LLVM IR (`.ll`/`.bc`) only for
now; DXIL/SPIR-V import follows `feme-run`'s own precedent once a test
needs it.

Both fixture formats — the scene it reads and the images it reads and writes —
are specified in "Textual scene and image fixtures" in
[../Design.md](../Design.md), because the graphics unit tests and both API
runtime test suites consume the same formats. This page documents the tool.

It is a separate binary rather than a `feme-run --draw` mode on purpose: a
dispatch is described by a group count and a resource heap, a draw by a
pipeline, attachments and vertex streams, and one tool carrying both argument
models would serve neither well.

Like `feme-opt` and `feme-run`, `feme-render` is a testing-oriented tool and
may use `llvm::cl::opt` freely.

**Scope:** `feme-render` renders off screen. It has no window, no surface and
no presentation path, and it never will — presentation belongs to the API
runtimes (see [../FeMeVulkanDesign.md](../FeMeVulkanDesign.md)'s WSI
decision), not to a `lit` fixture runner.

## OPTIONS

- `--wave-size=<N>`: the wave size to compile every stage at. `0` (the
  default) resolves it from the host, matching `feme-run`.
- `--workers=<N>`: the number of tile workers. Output must be identical for
  every value; varying it is the metamorphic check for the deterministic
  parallel schedule (see "Determinism and Reference Execution" in
  [../FeMeGraphicsDesign.md](../FeMeGraphicsDesign.md)).
- `--tile-order=<order>`: the tile traversal order. Also required to leave
  output unchanged, for the same reason.
- `--reference`: runs the scalar reference path instead of the SIMD one, the
  ground truth a differential test diffs against.
- `--dump=<name>`: print attachment `<name>` after the last draw. May be
  repeated; the default is every color attachment.
- `--expect=<file>`: compare the produced attachments against a checked-in
  image fixture instead of (or in addition to) printing them, and fail with a
  per-texel diff on mismatch.
- `--tolerance=<eps>`: allow a per-component absolute difference when
  comparing. Defaults to exact. This exists for cross-implementation
  differentials against lavapipe and WARP, where the specification permits
  variation; a FeMe-versus-FeMe comparison must never need it.
- `-O<n>`: the optimization level each stage is compiled at, wired the same
  way `feme-run -O` is.

## OUTPUT

Each dumped attachment is printed as an image fixture, in exactly the format
`--expect` and the `textures:` scene key accept, so an actual-output dump can
be pasted into a `CHECK` line and a rendered attachment can become the next
test's input texture:

```text
image color0 4x4 r8g8b8a8-unorm
  y=0: ff0000ff ff0000ff 000000ff 000000ff
  y=1: ff0000ff 000000ff 000000ff 000000ff
  y=2: 000000ff 000000ff 000000ff 000000ff
  y=3: 000000ff 000000ff 000000ff 000000ff
```

## EXAMPLES

```shell
# Render a scene and check the rasterized coverage of one triangle.
feme-render triangle.yaml | FileCheck triangle.yaml

# Check that the parallel tiled schedule is deterministic.
feme-render --workers=1 triangle.yaml > one.image
feme-render --workers=8 triangle.yaml > many.image
diff one.image many.image

# Diff the SIMD path against the scalar reference.
feme-render --reference triangle.yaml > ref.image
feme-render --expect=ref.image triangle.yaml
```

## EXIT STATUS

`feme-render` returns 0 on success, and a non-zero value if the scene fails to
parse, names state the executor does not implement, fails to compile a stage,
or produces attachments that do not match `--expect`.
