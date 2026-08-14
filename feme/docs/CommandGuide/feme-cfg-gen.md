# `feme-cfg-gen` — seeded CFG-shaped shader generator

## SYNOPSIS

```shell
feme-cfg-gen [options] -o <output file>
```

## DESCRIPTION

`feme-cfg-gen` is the command-line front end for
`feme::cpu::generateCFGIR`: roadmap milestone 5's layer 3 generator (see
the "CFG restructurization test suite" section of
[../FeMeCPUDesign.md](../FeMeCPUDesign.md)). It prints a shader-shaped
compute entry point's textual LLVM IR, deterministic given `--seed`: a
random nesting of uniform (group-id-derived) and divergent
(thread-id-derived) `if`s, loops with random break/continue placement, and,
behind `--unstructured`, unstructured edges that make the result
irreducible.

The named-shape corpus under `feme/test/Transforms/CPU/CFG/` will not cover
what real optimized DXIL does to a CFG; `feme-cfg-gen` exists to generate
many more shapes than anyone would hand-write, for the differential harness
(see [feme-run.md](feme-run.md)'s `--reference` option) and
`feme-cpu-restructure-fuzzer` to run.

Every generated block folds its own id into a per-invocation accumulator,
written to a raw-buffer UAV at the end -- *the output buffer is a trace of
the path each invocation took*, which is what makes a mismatch between two
runs diagnosable rather than merely detectable.

Every generated shape is guaranteed to terminate for any thread/group id, at
every option combination: a counted loop always has a small, compile-time
constant trip count, and `--unstructured`'s irreducible-edge construct
bounds its own two-block bounce with a shared counter (see CFGGen.cpp's
`genIrreducible`) rather than relying on either block's random,
`%tid`/`%gid`-derived exit condition alone -- that condition does not
change across a hop, so a thread for which it was never true would
otherwise bounce forever.

Like `feme-opt`/`feme-run`, `feme-cfg-gen` is a testing-oriented tool and
may use `llvm::cl::opt` freely.

## OPTIONS

- `--seed=<N>`: seeds the generator's PRNG (default `0`); the same seed
  always produces the same output.
- `--max-depth=<N>`: how deeply constructs may nest (default `3`).
- `--max-constructs=<N>`: the construct budget the generator spends
  (default `12`), bounding the output's size regardless of how the random
  choices along the way fall.
- `--divergent`/`--no-divergent`: allow divergent (thread-id-derived)
  branch conditions (default: allowed).
- `--loops`/`--no-loops`: allow loops with random break/continue placement
  (default: allowed).
- `--unstructured`/`--no-unstructured`: allow unstructured edges that make
  the result irreducible (default: not allowed -- see the "CFG
  restructurization test suite" section of
  [../FeMeCPUDesign.md](../FeMeCPUDesign.md) for why the differential
  harness leaves this off).
- `-o <file>`: output filename (default: stdout).
