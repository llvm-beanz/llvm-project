# dxil-as container output

This guide describes compute-shader container output for users assembling DXC's
legacy LLVM IR.

## Output formats

The default output remains LLVM 3.7 bitcode:

```sh
dxil-as shader.ll -o shader.bc
```

Container output uses LLVM's MC serializers without converting the legacy IR
to modern LLVM IR:

```sh
dxil-as --filetype=dxcontainer shader.ll -o shader.dxil
```

Without `-o`, file inputs produce `.bc` or `.dxil` files.
Standard input and `-o -` use standard output.
`--disable-output` verifies container metadata without writing a file.
`--preserve-bc-uselistorder` applies to embedded bitcode as well as raw output.

## Supported inputs

Container output supports one compute entry point, shader models
`cs_6_0` through `cs_6_7`, and validator versions `1.0` through `1.8`.
Inputs must contain `dx.shaderModel`, `dx.version`, `dx.valver`, and
`dx.entryPoints` metadata.
The DXIL version must match the shader model.
The validator version cannot precede the shader model's corresponding DXIL version.

The assembler reads resource bindings from `dx.resources` or entry-point
metadata.
Supported entry properties are shader flags, thread-group dimensions, and
fixed wave size.
Compute signatures must be empty.
Unsupported properties, resource extensions, root signatures, debug metadata,
and non-compute profiles produce errors.
Malformed metadata produces errors even with `--disable-verify`.

The container contains `SFI0`, `ISG1`, `OSG1`, `PSV0`, and `DXIL`.
Validator versions 1.5 and later also receive a `HASH` part.
PSV layouts follow the declared validator version.
Reflection, embedded root signatures, and debug/PDB parts are not generated.
The assembler packages existing DXIL and does not compile HLSL or perform
DXIL semantic verification.

## Signing

Output containers are unsigned: the container-header digest contains zeros.
The `HASH` part is the shader bitcode's MD5, not a container signature.
Use a compatible DXC validator to verify and sign the container before runtime use:

```sh
dxv shader.dxil -o=signed.dxil
```
