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

The build is currently producing a bunch of warnings, can you fix them?

```
1572/1661] Building CXX object tools/feme/lib/Translate/DXSA/CMakeFiles/obj.FeMeTranslateDXSA.dir/DXSAToLLVMIRTranslator.cpp.o
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3697:65: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3697 |     SampleForm Form{DXILOp::Sample, "sample", {}, /*Clamp=*/true};
      |                                                                 ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3703:65: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3703 |     SampleForm Form{DXILOp::Sample, "sample", {}, /*Clamp=*/true};
      |                                                                 ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3711:72: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3711 |     SampleForm Form{DXILOp::SampleLevel, "sampleLevel", {S.getSrcLod()}};
      |                                                                        ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3717:72: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3717 |     SampleForm Form{DXILOp::SampleLevel, "sampleLevel", {S.getSrcLod()}};
      |                                                                        ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3727:35: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3727 |                     /*Clamp=*/true};
      |                                   ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3736:35: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3736 |                     /*Clamp=*/true};
      |                                   ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3744:73: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3744 |     SampleForm Form{DXILOp::SampleGrad, "sampleGrad", {}, /*Clamp=*/true};
      |                                                                         ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3751:73: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3751 |     SampleForm Form{DXILOp::SampleGrad, "sampleGrad", {}, /*Clamp=*/true};
      |                                                                         ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3763:35: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3763 |                     /*Clamp=*/true};
      |                                   ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3772:35: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3772 |                     /*Clamp=*/true};
      |                                   ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3782:47: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3782 |                     {S.getSrcReferenceValue()}};
      |                                               ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3790:47: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3790 |                     {S.getSrcReferenceValue()}};
      |                                               ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3799:43: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3799 |                     /*NarrowOffsets=*/true};
      |                                           ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3807:43: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3807 |                     /*NarrowOffsets=*/true};
      |                                           ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3819:43: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3819 |                     /*NarrowOffsets=*/true};
      |                                           ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3830:43: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3830 |                     /*NarrowOffsets=*/true};
      |                                           ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3839:43: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3839 |                     /*NarrowOffsets=*/true};
      |                                           ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3848:43: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3848 |                     /*NarrowOffsets=*/true};
      |                                           ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3861:43: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3861 |                     /*NarrowOffsets=*/true};
      |                                           ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3873:43: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3873 |                     /*NarrowOffsets=*/true};
      |                                           ^
/Users/cbieneman/dev/llvm-project/feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp:3881:61: warning: missing field 'Gradients' initializer [-Wmissing-field-initializers]
 3881 |     SampleForm Form{DXILOp::CalculateLOD, "calculateLOD", {}};
      |
```
