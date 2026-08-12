# `feme-cpu-restructure-fuzzer` — fuzzer for CFG restructurization

## SYNOPSIS

```shell
feme-cpu-restructure-fuzzer [libFuzzer options] [corpus dir...]
```

## DESCRIPTION

`feme-cpu-restructure-fuzzer` is a
[libFuzzer](../../../llvm/docs/LibFuzzer.md) harness for roadmap milestone
5's "CFG restructurization test suite" (see
[../FeMeCPUDesign.md](../FeMeCPUDesign.md)): it interprets its input as a
[`feme-cfg-gen`](feme-cfg-gen.md) (`feme::cpu::generateCFGIR`) seed, plus a
handful of small option knobs (see `feme::cpu::CFGGenOptions`), rather than
raw IR text directly -- libFuzzer mutates bytes, and a generator seed is
exactly the kind of small, structured input that mutates into other valid
ones.

Each iteration generates a shader, runs it through
`feme::cpu::PreparePass`, and asserts `feme::cpu::verifyStructured`'s
postconditions on the result -- the same properties the named-shape corpus
(`feme/test/Transforms/CPU/CFG/`) and its `-verify-structured` `lit` tests
check by hand, just over many more (and, with `AllowUnstructured` always
on, more adversarial) shapes than anyone would hand-write. `FixIrreducible`
+ `StructurizeCFG` are in-tree, target-independent passes not otherwise
tested against shader-shaped input at this scale, which is exactly the risk
this suite exists to cover.

A failing seed reduces to a new file in `feme/test/Transforms/CPU/CFG/`, by
hand or with `llvm-reduce`.

## SEE ALSO

[feme-cfg-gen](feme-cfg-gen.md), [feme-opt](feme-opt.md)
