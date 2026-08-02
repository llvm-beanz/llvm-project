# `feme-spirv-import-fuzzer` seed corpus

This directory holds a small seed corpus for `feme-spirv-import-fuzzer`:
valid SPIR-V binaries (`*.spv`) the fuzzer can start mutating from, instead
of an empty corpus. This is the one place in the FeMe tree where checked-in
binary fixtures are expected (see "Avoiding binary test fixtures" in
[../../../docs/Design.md](../../../docs/Design.md)) — fuzzer corpora are
required to contain real binary samples.

Each `*.spv` file has a matching `*.mlir` file with the human-readable
`spirv` dialect source it was generated from, so the corpus stays
diffable/reviewable like the rest of the tree, and can be regenerated (e.g.
after a breaking change to the SPIR-V binary format) with `feme-translate`:

```shell
feme-translate --no-implicit-module --serialize-spirv minimal.mlir -o minimal.spv
feme-translate --no-implicit-module --serialize-spirv constant.mlir -o constant.spv
```

See [../../../docs/CommandGuide/feme-spirv-import-fuzzer.md](../../../docs/CommandGuide/feme-spirv-import-fuzzer.md)
for how to run the fuzzer against this corpus.
