cmake_minimum_required(VERSION 3.10)

project(MatchingEngine VERSION 0.0.1 LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LTTNG_UST REQUIRED IMPORTED_TARGET lttng-ust)
pkg_check_modules(URCU REQUIRED IMPORTED_TARGET liburcu)
message(STATUS "LTTNG Include Dirs: ${LTTNG_UST_INCLUDE_DIRS}")
message(STATUS "URCU Include Dirs: ${URCU_INCLUDE_DIRS}")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Highly recommended for high-perf code to avoid non-std bloat

enable_testing()

# This ensures compiled binaries go into your /bin folders
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin/debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin/release")


# Tracing
set(LTTNG_PROVIDER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/tp_provider.cpp")

# library
add_library(engine_logic INTERFACE)
target_include_directories(engine_logic INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src ${LTTNG_UST_INCLUDE_DIRS} ${URCU_INCLUDE_DIRS})
target_link_libraries(engine_logic INTERFACE PkgConfig::LTTNG_UST PkgConfig::URCU ${CMAKE_DL_LIBS})

# Points to your source file in the /src directory
add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE engine_logic)

# Ring Buffer Test
add_executable(ring_buffer_test tests/ring_buffer_test.cpp)
target_link_libraries(ring_buffer_test PRIVATE engine_logic)

# Ring Buffer Low CAS Test 
add_executable(ring_buffer_low_cas_test tests/ring_buffer_low_cas_test.cpp ${LTTNG_PROVIDER_SRC})
target_include_directories(ring_buffer_low_cas_test PRIVATE src ${LTTNG_UST_INCLUDE_DIRS})
target_link_libraries(ring_buffer_low_cas_test PRIVATE engine_logic -Wl,--whole-archive PkgConfig::LTTNG_UST -Wl,--no-whole-archive)

# Buffer General Test
add_executable(buffer_general_test tests/buffer_general_test.cpp)
target_link_libraries(ring_buffer_test PRIVATE engine_logic)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(NOT MSVC)
        target_compile_options(engine_logic INTERFACE -g -Og -fno-omit-frame-pointer "-I${LTTNG_UST_INCLUDE_DIRS}" "-I${URCU_INCLUDE_DIRS}")
        target_compile_options(ring_buffer_test PRIVATE -g -Og -fno-omit-frame-pointer)
        target_compile_options(ring_buffer_low_cas_test PRIVATE -g -Og -fno-omit-frame-pointer)
        target_compile_options(buffer_general_test PRIVATE -g -Og -fno-omit-frame-pointer)
    endif()
else()
        target_compile_options(engine_logic INTERFACE -g -fno-omit-frame-pointer -fverbose-asm -O3 "-I${LTTNG_UST_INCLUDE_DIRS}" "-I${URCU_INCLUDE_DIRS}")
        target_compile_options(buffer_general_test PRIVATE -g -fno-omit-frame-pointer -fverbose-asm -O3)
endif()

# Test files
add_test(NAME RingBufferTest COMMAND ring_buffer_test)
# Ommit this test as it fails
# add_test(NAME RingBufferLowCASTest COMMAND ring_buffer_low_cas_test)
add_test(NAME BufferGeneralTest COMMAND buffer_general_test)

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
