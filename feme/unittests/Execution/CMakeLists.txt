set(LLVM_LINK_COMPONENTS
  Support
  TestingSupport
  )

add_feme_unittest(FeMeExecutionTests
  DeferredDestroyRefCountedTest.cpp
  TaskExecutorTest.cpp
  )
target_link_libraries(FeMeExecutionTests
  PRIVATE
  FeMeExecution
  )
