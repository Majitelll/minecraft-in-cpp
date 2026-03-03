cmake_minimum_required(VERSION 3.10)

if(POLICY CMP0072)
    cmake_policy(SET CMP0072 NEW)
endif()

project(game VERSION 1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ── GLFW (cross-platform, disable platform backends we don't need) ──────────
if(WIN32)
    set(GLFW_BUILD_WIN32 ON  CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11     OFF CACHE BOOL "" FORCE)
elseif(APPLE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11     OFF CACHE BOOL "" FORCE)
else()  # Linux
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11     ON  CACHE BOOL "" FORCE)
endif()

add_subdirectory(vendors/glfw)
add_subdirectory(vendors/cglm)

# ── Vulkan ───────────────────────────────────────────────────────────────────
find_package(Vulkan REQUIRED)

# ── Shader compilation ───────────────────────────────────────────────────────
# Requires glslc (ships with the Vulkan SDK) to be on PATH
set(SHADER_DIR ${CMAKE_SOURCE_DIR}/src/shaders)
set(SHADER_OUT ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT})

set(VERT_SHADER ${SHADER_DIR}/vert.vert)
set(FRAG_SHADER ${SHADER_DIR}/frag.frag)
set(VERT_SPV    ${SHADER_OUT}/vert.spv)
set(FRAG_SPV    ${SHADER_OUT}/frag.spv)

add_custom_command(
    OUTPUT  ${VERT_SPV}
    COMMAND Vulkan::glslc ${VERT_SHADER} -o ${VERT_SPV}
    DEPENDS ${VERT_SHADER}
    COMMENT "Compiling vertex shader"
)
add_custom_command(
    OUTPUT  ${FRAG_SPV}
    COMMAND Vulkan::glslc ${FRAG_SHADER} -o ${FRAG_SPV}
    DEPENDS ${FRAG_SHADER}
    COMMENT "Compiling fragment shader"
)
add_custom_target(shaders ALL DEPENDS ${VERT_SPV} ${FRAG_SPV})

# ── Executable ───────────────────────────────────────────────────────────────
add_executable(game
    src/main.cpp
)

add_dependencies(game shaders)

target_include_directories(game PRIVATE
    include
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(game PRIVATE
    glfw
    Vulkan::Vulkan
    cglm
)

# Copy compiled shaders next to the executable after build
add_custom_command(TARGET game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SHADER_OUT}
        $<TARGET_FILE_DIR:game>/shaders
    COMMENT "Copying shaders to output directory"
)