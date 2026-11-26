add_feme_unittest(FeMeDirect3DTests
  RootSignatureTest.cpp
)

target_link_libraries(FeMeDirect3DTests PRIVATE FeMeDirect3D LLVMSupport)
