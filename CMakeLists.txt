cmake_minimum_required(VERSION 3.9.4)
project(t3d2map C)

set(CMAKE_C_STANDARD 11)

# Set all of our output directories.
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib/)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

function(build_platform)
    set(PL_USE_GRAPHICS OFF CACHE BOOL "Graphics")
    set(PL_USE_MODEL OFF CACHE BOOL "Model")
    add_subdirectory(platform/platform)
endfunction()

build_platform()

add_executable(t3d2map main.c)
add_dependencies(t3d2map platform)

target_link_libraries(t3d2map platform)