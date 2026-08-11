# FeMe: FrontEnd for the MiddleEnd

FeMe is an LLVM sub-project for importing, translating, and retargeting GPU
shader intermediate representations (SPIR-V, DXIL, DXBC) using LLVM/MLIR
infrastructure. See [docs/Design.md](docs/Design.md) for the full design and
roadmap, [docs/FeMeCPUDesign.md](docs/FeMeCPUDesign.md) for the (proposed)
CPU target design, and [docs/CommandGuide](docs/CommandGuide/index.md) for
usage docs for each of FeMe's command line tools.

FeMe is currently under initial development (see the Roadmap / Milestones
section of the design document) and is not yet part of the default `"all"`
project set; enable it explicitly with:

```shell
cmake -S llvm -B build -DLLVM_ENABLE_PROJECTS=feme ...
ninja -C build check-feme
```

Building feme also implicitly enables MLIR, which it depends on.
