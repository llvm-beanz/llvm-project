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

Also please run the Vulkan CTS from the checkout under /home/dev/dev/VK-GL-CTS/
after each change and update the VulkanCTSReport.md.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

I have an HLSL shader that I'd like to be able to compile through from DXIL to
AMD ISA, but feme is hitting an assert when I try.

The shader is:

```
Texture2D<half4> InputTexture : register(t0);
RWTexture2D<half4> OutputTexture : register(u0);

cbuffer FilterParameters : register(b0)
{
    half SpatialScale;
    half ColorScale;
};

[numthreads(8, 8, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    uint width = 2048, height = 2048;
    //OutputTexture.GetDimensions(width, height);

    int2 pixel = int2(threadID.xy);
    if (pixel.x >= width || pixel.y >= height)
        return;

    half3 center = InputTexture.Load(int3(pixel, 0)).rgb;
    half3 weightedSum = 0.0h;
    half totalWeight = 0.0h;

    [unroll]
    for (int y = -4; y <= 4; ++y)
    {
        [unroll]
        for (int x = -4; x <= 4; ++x)
        {
            int2 coordinate = clamp(
                pixel + int2(x, y),
                int2(0, 0),
                int2(width, height) - 1);

            half3 color = InputTexture.Load(int3(coordinate, 0)).rgb;
            half3 difference = color - center;
            half2 offset = half2(x, y);

            half distance =
                dot(offset, offset) * SpatialScale +
                dot(difference, difference) * ColorScale;

            half weight = exp2(-distance);
            weightedSum += color * weight;
            totalWeight += weight;
        }
    }

    OutputTexture[pixel] =
        half4(weightedSum / max(totalWeight, 0.0001h), 1.0h);
}
```

It can be compiled with DXC as a compute shader with the `-T cs_6_8 -enable-16bit-types` flags.

When I try to compile that DXIL to AMD ISA with feme using the command
`./bin/feme --target=amdgpu9.0a-amd-amdhsa workload-68.dxil -o workload-68.o`
I get the assert output:

```
Unknown target ext type!
UNREACHABLE executed at /Users/cbieneman/dev/llvm-project/llvm/lib/CodeGen/ValueTypes.cpp:287!
fish: Job 1, './bin/feme --target=amdgpu9.0a-…' terminated by signal SIGABRT (Abort)
```

Can you diagnose and fix this issue?
