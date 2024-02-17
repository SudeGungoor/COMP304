cmake_minimum_required(VERSION 3.20)
project(shellect LANGUAGES C)

set(CMAKE_GENERATOR Ninja)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wno-comment -Werror -Wextra -Wpedantic)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(WARNING "CMake should not be executed in the root directory. Please create a build directory and run CMake there.")
endif()

add_executable(${PROJECT_NAME} src/shell-skeleton.c)

add_subdirectory(module)