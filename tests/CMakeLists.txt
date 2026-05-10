add_executable(mbpf_tests unit/test_basic.cpp)
target_link_libraries(mbpf_tests PRIVATE mbpf)
target_include_directories(mbpf_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)

if(MBPF_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mbpf_tests PRIVATE -O0 -g --coverage)
    target_link_options(mbpf_tests PRIVATE --coverage)
endif()

add_test(NAME mbpf_tests COMMAND mbpf_tests)

add_executable(test_vm_invalid unit/test_vm_invalid.cpp)
target_link_libraries(test_vm_invalid PRIVATE mbpf)
target_include_directories(test_vm_invalid PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)
set_target_properties(test_vm_invalid PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
add_test(NAME test_vm_invalid COMMAND ${CMAKE_BINARY_DIR}/tests/test_vm_invalid)
