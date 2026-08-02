add_llvm_executable(feme feme.cpp)

llvm_update_compile_flags(feme)
target_link_libraries(feme PRIVATE LLVMSupport)
