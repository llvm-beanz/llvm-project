add_feme_library(FeMeDirect3D
  AdvancedGraphicsPlan.cpp
  RayTracingPlan.cpp
  PresentationPlan.cpp
  CapabilityInventory.cpp
  ComputePlan.cpp
  ExecutionPlan.cpp
  ObjectModel.cpp
  ResidencyPlan.cpp
  BarrierPlan.cpp
  DescriptorHeap.cpp
  ResourcePlan.cpp
  PipelinePlan.cpp
  RootSignature.cpp

  LINK_LIBS
  LLVMSupport
)
