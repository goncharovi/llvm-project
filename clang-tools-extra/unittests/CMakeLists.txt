add_custom_target(ExtraToolsUnitTests)
set_target_properties(ExtraToolsUnitTests PROPERTIES FOLDER "Extra Tools Unit Tests")

function(add_extra_unittest test_dirname)
  add_unittest(ExtraToolsUnitTests ${test_dirname} ${ARGN})
endfunction()

if (CXX_SUPPORTS_SUGGEST_OVERRIDE_FLAG)
  add_compile_options("-Wno-suggest-override")
endif()

add_subdirectory(clang-apply-replacements)
add_subdirectory(clang-change-namespace)
add_subdirectory(clang-doc)
add_subdirectory(clang-include-fixer)
add_subdirectory(clang-move)
add_subdirectory(clang-query)
add_subdirectory(clang-tidy)
