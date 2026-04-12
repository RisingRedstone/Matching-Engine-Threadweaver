cmake_minimum_required(VERSION 3.10)

project(MatchingEngine VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

# This ensures compiled binaries go into your /bin folders
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin/debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin/release")

# library
add_library(engine_logic INTERFACE)
target_include_directories(engine_logic INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Points to your source file in the /src directory
add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE engine_logic)

# Ring Buffer Test
add_executable(ring_buffer_test tests/ring_buffer_test.cpp)
target_link_libraries(ring_buffer_test PRIVATE engine_logic)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(NOT MSVC)
        target_compile_options(engine_logic INTERFACE -g -O3 -fno-omit-frame-pointer)
        target_compile_options(ring_buffer_test PRIVATE -g -O3 -fno-omit-frame-pointer)
    endif()
endif()

# Test files
add_test(NAME RingBufferTest COMMAND ring_buffer_test)

# 5. Optional: Compiler Warnings
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(Doxygen)

if (DOXYGEN_FOUND)
    # This creates a 'doc' target
    add_custom_target(doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM)
endif()
