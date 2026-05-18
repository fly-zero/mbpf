cmake_minimum_required(VERSION 3.16)
project(m_bpf LANGUAGES C CXX)

option(MBPF_ENABLE_COVERAGE "Enable gcov coverage flags" OFF)
option(MBPF_ENABLE_TESTS "Build and run tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

BISON_TARGET(MBPFParser
    ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/expression_parser.y
    ${GENERATED_DIR}/expression_parser.cpp
    DEFINES_FILE ${GENERATED_DIR}/expression_parser.hpp)

FLEX_TARGET(MBPFLexer
    ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/expression_lexer.l
    ${GENERATED_DIR}/expression_lexer.cpp)

ADD_FLEX_BISON_DEPENDENCY(MBPFLexer MBPFParser)

add_library(mbpf_core STATIC
    src/common/error.cpp
    src/registry/qualifier_registry.cpp
    src/frontend/ast.cpp
    src/frontend/parser_driver.cpp
    src/codegen/compiler.cpp
    src/vm/vm.cpp
    ${BISON_MBPFParser_OUTPUTS}
    ${FLEX_MBPFLexer_OUTPUTS})

target_include_directories(mbpf_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${GENERATED_DIR})

if(MBPF_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mbpf_core PRIVATE -O0 -g --coverage)
    target_link_options(mbpf_core PRIVATE --coverage)
endif()

add_library(mbpf STATIC src/c_api/mbpf_c_api.cpp)
target_include_directories(mbpf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(mbpf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${GENERATED_DIR})
target_link_libraries(mbpf PRIVATE mbpf_core)

if(MBPF_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mbpf PRIVATE -O0 -g --coverage)
    target_link_options(mbpf PRIVATE --coverage)
endif()

if(MBPF_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

find_program(GCOV_BIN gcov)
if(MBPF_ENABLE_COVERAGE AND MBPF_ENABLE_TESTS AND GCOV_BIN)
    add_custom_target(coverage
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMAND ${GCOV_BIN} -r
                ${CMAKE_CURRENT_SOURCE_DIR}/src/common/error.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/registry/qualifier_registry.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/ast.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/parser_driver.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/compiler.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/vm/vm.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/c_api/mbpf_c_api.cpp
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Run tests and generate gcov report")
endif()
