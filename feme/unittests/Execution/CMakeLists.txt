set(LLVM_LINK_COMPONENTS
  Support
  TestingSupport
  )

add_feme_unittest(FeMeExecutionTests
  TaskExecutorTest.cpp
  )
target_link_libraries(FeMeExecutionTests
  PRIVATE
  FeMeExecution
  )
