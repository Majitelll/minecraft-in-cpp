cmake_minimum_required(VERSION 3.10)

if(POLICY CMP0072)
    cmake_policy(SET CMP0072 NEW)
endif()

# Project renamed to 'game'
project(game VERSION 1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "Disable Wayland backend in GLFW")
set(GLFW_BUILD_X11 ON CACHE BOOL "Enable X11 backend in GLFW")

add_subdirectory(vendors/glfw)
add_subdirectory(vendors/cglm) 

find_package(OpenGL REQUIRED)

# Executable renamed to 'game'
add_executable(game
    src/main.cpp
    src/glad.c
)

target_include_directories(game PRIVATE include)

target_link_libraries(game
    PRIVATE glfw OpenGL::GL cglm
)