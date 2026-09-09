---
model: gpt-5.6-sol
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Verify your changes by building and testing using the
/opt/llvm-tooling/Config.cmake cache file with CMake's -C flag to configure the
build. Test the compiler and runtime support with the targets: check-llvm,
check-clang, check-hlsl-vk and check-hlsl-clang-vk.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository and commit it in its own commit when you're done.

# Background

**Description** `OpSwitch` even as defined with
[SPV_KHR_maximal_reconvergence](https://github.com/KhronosGroup/SPIRV-Registry/blob/main/extensions/KHR/SPV_KHR_maximal_reconvergence.asciidoc#divergence),
doesn't require converging on switch cases that have fall through.

The only way to get SPIR-V to implement switch statements that converge
correctly is with `OpBranch` instead.

**Steps to Reproduce**

Given the following HLSL:
```hlsl
RWBuffer<int> value;

[numthreads(4, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID) {
  uint sum = 0;
  switch (value[threadID.x]) {
    case 0:
      sum += WaveActiveSum(1);
    default:
      sum += WaveActiveSum(10);
      break;
  }
  value[threadID.x] = sum;
}
```

If given the input `[ 0, 0, 1, 2]`, the computed output should be `[ 42, 42, 40,
40 ]`.

**Actual Behavior**

Even with the KHR maximal reconvergence extension the `OpSwitch` is not
guaranteed to converge the tangles between case 0 and the default case.

However, if instead these were generated as a chain of `OpBranch` statements,
the control flow would converge at each new `OpBranch`, which would result in
the correct tangle grouping.

The HLSL example above does not compile in Clang today due to missing features,
a Clang example that is closer to compiling is https://godbolt.org/z/nbhbd8fdK.

Any switch with fallthrough cases that have behavior dependent on participating
lanes converging will be undefined behavior if the SPIRV `OpSwitch` instruction
is used, so we should just not generate it ever for HLSL.

Just to capture a thought here to guide work. @s-perron mentioned over on the
[DXC
issue](https://github.com/microsoft/DirectXShaderCompiler/issues/6957#issuecomment-2445046428)
a possible expansion that could implement this behavior.

I want to propose a slightly different expansion. Given this source:
```c
switch(selector) {
  default:
    something();
  case 1:
  case 3:
    somethingElse();
    break;
  case 2:
    anotherThing();
    break;
}
```

It should be accurate to expand it to:
```c
do {
  if (!(selector == 1 && selector == 2 && selector == 3)) {
    something();
    selector = 1; // This case falls through so we now need to execute case 1.
  }
  if (selector == 1) {
    somethingElse();
    break; // breaks can still be breaks;
  }
  if (selector == 2) {
    anotherThing();
    break;
  }
} while(false)
```

The advantage of this over the other expansion is that we don't rely on
`OpSwitch` at all. This does produce duplicate conditional checks, but I expect
LLVM's CSE will clean that up where it can. This should also be pretty simple to
generate directly from Clang which might just be a good across the board
approach for HLSL & OpenCL.

# Request

Based on the background information provided, please implement a new option for
clang code generation to avoid switch generation. The option should default on
for HLSL when targeting SPIR-V, but may also be useful to enable explicitly for
other use cases.
