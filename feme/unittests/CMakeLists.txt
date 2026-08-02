add_custom_target(FeMeUnitTests)
set_target_properties(FeMeUnitTests PROPERTIES FOLDER "FeMe/Tests")

function(add_feme_unittest test_dirname)
  add_unittest(FeMeUnitTests ${test_dirname} ${ARGN})
endfunction()

add_subdirectory(Core)
