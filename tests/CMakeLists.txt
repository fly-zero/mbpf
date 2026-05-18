include(FetchContent)
include(GoogleTest)

find_package(GTest QUIET)

if(NOT GTest_FOUND)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(mbpf_tests unit/test_basic.cpp)
target_link_libraries(mbpf_tests PRIVATE mbpf GTest::gtest_main)
target_include_directories(mbpf_tests PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)

if(MBPF_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mbpf_tests PRIVATE -O0 -g --coverage)
    target_link_options(mbpf_tests PRIVATE --coverage)
endif()

gtest_discover_tests(mbpf_tests)

add_executable(test_vm_invalid unit/test_vm_invalid.cpp)
target_link_libraries(test_vm_invalid PRIVATE mbpf GTest::gtest_main)
target_include_directories(test_vm_invalid PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)

if(MBPF_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_vm_invalid PRIVATE -O0 -g --coverage)
    target_link_options(test_vm_invalid PRIVATE --coverage)
endif()

gtest_discover_tests(test_vm_invalid)
