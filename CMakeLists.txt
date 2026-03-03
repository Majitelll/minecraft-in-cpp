cmake_minimum_required(VERSION 3.20)
project(MinecraftVulkan CXX)
set(CMAKE_CXX_STANDARD 17)

# ── Vulkan ────────────────────────────────────────────────────────────────────
find_package(Vulkan REQUIRED)

# On macOS, prefer the MoltenVK SDK installed via the LunarG installer or Homebrew.
# The SDK sets VULKAN_SDK; if it's not set, fall back to Homebrew paths.
if(APPLE)
    if(NOT DEFINED ENV{VULKAN_SDK})
        # Try Homebrew MoltenVK
        execute_process(COMMAND brew --prefix molten-vk
            OUTPUT_VARIABLE MOLTENVK_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(MOLTENVK_PREFIX)
            message(STATUS "MoltenVK found via Homebrew at ${MOLTENVK_PREFIX}")
        else()
            message(WARNING "VULKAN_SDK not set and MoltenVK not found via Homebrew. "
                            "Install the LunarG Vulkan SDK or: brew install molten-vk")
        endif()
    endif()
endif()

# ── GLFW ──────────────────────────────────────────────────────────────────────
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

if(WIN32)
    set(GLFW_USE_WIN32 ON)
elseif(APPLE)
    # Use Cocoa/Metal backend — required for MoltenVK
    set(GLFW_USE_COCOA ON)
else()
    set(GLFW_USE_X11 ON)
endif()

add_subdirectory(external/glfw)

# ── cglm ──────────────────────────────────────────────────────────────────────
add_subdirectory(external/cglm)

# ── Sources ───────────────────────────────────────────────────────────────────
add_executable(game
    src/main.cpp
    src/app.cpp
    src/camera.cpp
    src/chunk.cpp
    src/chunkmanager.cpp
)

target_include_directories(game PRIVATE src)
target_link_libraries(game PRIVATE Vulkan::Vulkan glfw cglm)

# ── Threading ─────────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)
target_link_libraries(game PRIVATE Threads::Threads)

# ── macOS specifics ───────────────────────────────────────────────────────────
if(APPLE)
    # Link against the MoltenVK framework if using the LunarG SDK
    # (Vulkan::Vulkan already handles this when VULKAN_SDK is set correctly,
    # but we add the Metal/QuartzCore frameworks that MoltenVK needs)
    target_link_libraries(game PRIVATE
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Cocoa"
        "-framework IOKit"
    )
    # Tell the linker where to find MoltenVK dylib if SDK is set
    if(DEFINED ENV{VULKAN_SDK})
        target_link_directories(game PRIVATE $ENV{VULKAN_SDK}/lib)
    endif()
endif()

# ── Shader compilation ────────────────────────────────────────────────────────
# Allow overriding glslc path via -DGLSLC=/path/to/glslc (used by macOS CI)
if(NOT GLSLC)
    find_program(GLSLC glslc HINTS
        $ENV{VULKAN_SDK}/bin                  # Windows / Linux LunarG SDK
        /usr/bin                              # Linux system install
        /usr/local/bin                        # Homebrew (Intel Mac)
        /opt/homebrew/bin                     # Homebrew (Apple Silicon)
        /opt/homebrew/opt/shaderc/bin         # shaderc via Homebrew
    )
endif()

if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found. Install the Vulkan SDK or: brew install shaderc")
endif()

set(SHADER_DIR ${CMAKE_SOURCE_DIR}/src/shaders)
set(SHADER_OUT ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT})

set(VERT_SPV ${SHADER_OUT}/vert.spv)
set(FRAG_SPV ${SHADER_OUT}/frag.spv)

add_custom_command(
    OUTPUT ${VERT_SPV}
    COMMAND ${GLSLC} ${SHADER_DIR}/vert.vert -o ${VERT_SPV}
    DEPENDS ${SHADER_DIR}/vert.vert
)
add_custom_command(
    OUTPUT ${FRAG_SPV}
    COMMAND ${GLSLC} ${SHADER_DIR}/frag.frag -o ${FRAG_SPV}
    DEPENDS ${SHADER_DIR}/frag.frag
)
add_custom_target(shaders DEPENDS ${VERT_SPV} ${FRAG_SPV})
add_dependencies(game shaders)

# Copy compiled shaders next to the executable
add_custom_command(TARGET game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:game>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VERT_SPV} $<TARGET_FILE_DIR:game>/shaders/vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FRAG_SPV} $<TARGET_FILE_DIR:game>/shaders/frag.spv
)
