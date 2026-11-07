cmake_minimum_required(VERSION 3.20)
project(stocknavalon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# the engine is linked into the Python module, a shared library
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The engine: every source except the CLI's main
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS src/game/*.cc)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/main\\.cc$")
add_library(engine STATIC ${ENGINE_SOURCES})
target_include_directories(engine PUBLIC src/game)
target_compile_options(engine PRIVATE -Wall -Wextra)

if(SKBUILD)
    # pip install . (scikit-build-core): the Python module
    find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development.Module)
    find_package(nanobind CONFIG REQUIRED)
    nanobind_add_module(_engine NB_STATIC python/bindings.cc)
    target_link_libraries(_engine PRIVATE engine)
    install(TARGETS _engine LIBRARY DESTINATION stocknavalon)
else()
    # a plain CMake build: the CLI and the tests, like the Makefile
    add_executable(final_program src/game/main.cc)
    target_link_libraries(final_program PRIVATE engine)

    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cc)
    add_executable(run_tests ${TEST_SOURCES})
    target_include_directories(run_tests PRIVATE third_party)
    target_link_libraries(run_tests PRIVATE engine)
endif()
