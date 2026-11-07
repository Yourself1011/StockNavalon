cmake_minimum_required(VERSION 3.25)
project(stocknavalon LANGUAGES CXX)

# Builds are run through the presets in CMakePresets.json:
#   cmake --preset release && cmake --build --preset release
#   cmake --preset dev && cmake --build --preset dev && ctest --preset dev
# pip install . (scikit-build-core) uses this file too, for the Python module.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# the engine is linked into the Python module, a shared library
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(STOCKNAVALON_SANITIZE
       "Build with address and undefined-behaviour sanitizers and checked \
standard library containers (the dev preset)" OFF)
if(STOCKNAVALON_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                        # libstdc++ (gcc) and libc++ (clang, macOS) checks
                        -D_GLIBCXX_DEBUG
                        -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG)
    add_link_options(-fsanitize=address,undefined)
endif()

# warnings for our own code only, not nanobind's
function(stocknavalon_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra)
endfunction()

# The engine: every source except the CLI's main
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS src/game/*.cc)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/main\\.cc$")
add_library(engine STATIC ${ENGINE_SOURCES})
target_include_directories(engine PUBLIC src/game)
stocknavalon_warnings(engine)

# The Python module. Under pip it's the only target. In a normal build it's
# built too when nanobind can be found (in the project's .venv, see README),
# so ctest tests the module as it is now rather than the last pip install.
if(SKBUILD)
    set(STOCKNAVALON_PYTHON ON)
else()
    if(NOT DEFINED Python_EXECUTABLE AND EXISTS "${CMAKE_SOURCE_DIR}/.venv/bin/python")
        set(Python_EXECUTABLE "${CMAKE_SOURCE_DIR}/.venv/bin/python")
    endif()
    find_package(Python 3.9 COMPONENTS Interpreter Development.Module)
    if(Python_FOUND)
        execute_process(COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
                        OUTPUT_VARIABLE nanobind_ROOT
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
    if(nanobind_ROOT)
        set(STOCKNAVALON_PYTHON ON)
    else()
        message(STATUS "nanobind not found: skipping the Python module")
        set(STOCKNAVALON_PYTHON OFF)
    endif()
endif()

if(STOCKNAVALON_PYTHON)
    find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development.Module)
    find_package(nanobind CONFIG REQUIRED)
    nanobind_add_module(_engine NB_STATIC python/bindings.cc)
    target_link_libraries(_engine PRIVATE engine)
endif()

if(SKBUILD)
    install(TARGETS _engine LIBRARY DESTINATION stocknavalon)
    return()
endif()

# The CLI
add_executable(final_program src/game/main.cc)
target_link_libraries(final_program PRIVATE engine)
stocknavalon_warnings(final_program)

# Examples of using the engine as a library (docs/cpp.md)
foreach(example random_game one_ply_search)
    add_executable(${example} examples/${example}.cc)
    target_link_libraries(${example} PRIVATE engine)
    stocknavalon_warnings(${example})
endforeach()

# Tests, run with ctest. Both run from the repo root, where the fixed map
# file is.
enable_testing()

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cc)
add_executable(run_tests ${TEST_SOURCES})
target_include_directories(run_tests PRIVATE third_party)
target_link_libraries(run_tests PRIVATE engine)
stocknavalon_warnings(run_tests)
add_test(NAME cpp COMMAND run_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

if(STOCKNAVALON_PYTHON)
    # A package in the build tree: the module next to copies of the Python
    # files, put first on the path for the tests
    set(PACKAGE_DIR "${CMAKE_BINARY_DIR}/python/stocknavalon")
    set_target_properties(_engine PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${PACKAGE_DIR}")
    file(GLOB PACKAGE_FILES LIST_DIRECTORIES false CONFIGURE_DEPENDS
         python/stocknavalon/*)
    foreach(file ${PACKAGE_FILES})
        get_filename_component(name ${file} NAME)
        configure_file(${file} "${PACKAGE_DIR}/${name}" COPYONLY)
    endforeach()

    # A sanitized module can't be loaded into a normal Python, so the dev
    # build compiles it (for errors, and for the editor) without testing it
    if(NOT STOCKNAVALON_SANITIZE)
        add_test(NAME python
                 COMMAND "${Python_EXECUTABLE}" -m pytest -q tests/python
                         -p no:cacheprovider
                 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        set_tests_properties(python PROPERTIES
            ENVIRONMENT "STOCKNAVALON_BUILD_PACKAGE=${CMAKE_BINARY_DIR}/python")
    endif()
endif()
