cmake_minimum_required(VERSION 3.10)

if(POLICY CMP0072)
    cmake_policy(SET CMP0072 NEW)
endif()

project(GLFWWindowExample VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "Disable Wayland backend in GLFW")
set(GLFW_BUILD_X11 ON CACHE BOOL "Enable X11 backend in GLFW")

add_subdirectory(vendors/glfw)
add_subdirectory(vendors/cglm)  # <-- Add this line to include cglm

find_package(OpenGL REQUIRED)

add_executable(GLFWWindowExample
    src/main.cpp
    src/glad.c
)

target_include_directories(GLFWWindowExample PRIVATE include)

target_link_libraries(GLFWWindowExample
    PRIVATE glfw OpenGL::GL cglm  # <-- Link with cglm target
)