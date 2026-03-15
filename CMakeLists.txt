cmake_minimum_required(VERSION 3.20)
project(MinecraftVulkan CXX)
set(CMAKE_CXX_STANDARD 17)

# ── Common sources (shared between all builds) ────────────────────────────────
set(COMMON_SOURCES
    src/main.cpp
    src/app.cpp
    src/camera.cpp
    src/chunk.cpp
    src/chunkmanager.cpp
    src/textureatlas.cpp
)

# ─────────────────────────────────────────────────────────────────────────────
# Emscripten (WebGL2 / web build)
# ─────────────────────────────────────────────────────────────────────────────
if(EMSCRIPTEN)
    add_executable(game
        ${COMMON_SOURCES}
        src/renderer_gl.cpp
    )

    target_include_directories(game PRIVATE
        src
        external/cglm/include
    )

    # Link flags for Emscripten (no pthread — runs on GitHub Pages without COOP/COEP)
    set(EM_FLAGS
        "-sUSE_GLFW=3"
        "-sUSE_WEBGL2=1"
        "-sFULL_ES3=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
        "--bind"
        "--shell-file ${CMAKE_SOURCE_DIR}/src/shell.html"
    )

    string(JOIN " " EM_FLAGS_STR ${EM_FLAGS})
    set_target_properties(game PROPERTIES
        SUFFIX ".html"
        LINK_FLAGS "${EM_FLAGS_STR}"
    )

    # cglm header-only, no subdirectory needed for Emscripten
    add_subdirectory(external/cglm)
    target_link_libraries(game PRIVATE cglm)

# ─────────────────────────────────────────────────────────────────────────────
# Desktop (Vulkan)
# ─────────────────────────────────────────────────────────────────────────────
else()
    # ── Vulkan ────────────────────────────────────────────────────────────────
    find_package(Vulkan REQUIRED)

    if(APPLE)
        if(NOT DEFINED ENV{VULKAN_SDK})
            execute_process(COMMAND brew --prefix molten-vk
                OUTPUT_VARIABLE MOLTENVK_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(MOLTENVK_PREFIX)
                message(STATUS "MoltenVK found via Homebrew at ${MOLTENVK_PREFIX}")
            else()
                message(WARNING "VULKAN_SDK not set and MoltenVK not found via Homebrew.")
            endif()
        endif()
    endif()

    # ── GLFW ──────────────────────────────────────────────────────────────────
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    if(WIN32)
        set(GLFW_USE_WIN32 ON)
    elseif(APPLE)
        set(GLFW_USE_COCOA ON)
    else()
        set(GLFW_USE_X11 ON)
    endif()

    add_subdirectory(external/glfw)
    add_subdirectory(external/cglm)

    add_executable(game
        ${COMMON_SOURCES}
        src/renderer_vulkan.cpp
    )

    # Windows: hide terminal in Release, keep it in Debug
    if(WIN32)
        set_target_properties(game PROPERTIES
            WIN32_EXECUTABLE $<CONFIG:Release>
        )
    endif()

    target_include_directories(game PRIVATE src)
    target_link_libraries(game PRIVATE Vulkan::Vulkan glfw cglm)

    find_package(Threads REQUIRED)
    target_link_libraries(game PRIVATE Threads::Threads)

    if(APPLE)
        target_link_libraries(game PRIVATE
            "-framework Metal"
            "-framework QuartzCore"
            "-framework Cocoa"
            "-framework IOKit"
        )
        if(DEFINED ENV{VULKAN_SDK})
            target_link_directories(game PRIVATE $ENV{VULKAN_SDK}/lib)
        endif()
    endif()

    # ── Shader compilation ────────────────────────────────────────────────────
    if(NOT GLSLC)
        find_program(GLSLC glslc HINTS
            $ENV{VULKAN_SDK}/bin
            /usr/bin
            /usr/local/bin
            /opt/homebrew/bin
            /opt/homebrew/opt/shaderc/bin
        )
    endif()

    if(NOT GLSLC)
        message(FATAL_ERROR "glslc not found. Install the Vulkan SDK or: brew install shaderc")
    endif()

    set(SHADER_DIR ${CMAKE_SOURCE_DIR}/src/shaders)
    set(SHADER_OUT ${CMAKE_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${SHADER_OUT})

    set(VERT_SPV    ${SHADER_OUT}/vert.spv)
    set(FRAG_SPV    ${SHADER_OUT}/frag.spv)
    set(UI_VERT_SPV ${SHADER_OUT}/ui_vert.spv)
    set(UI_FRAG_SPV ${SHADER_OUT}/ui_frag.spv)

    add_custom_command(OUTPUT ${VERT_SPV}
        COMMAND ${GLSLC} ${SHADER_DIR}/vert.vert -o ${VERT_SPV}
        DEPENDS ${SHADER_DIR}/vert.vert)
    add_custom_command(OUTPUT ${FRAG_SPV}
        COMMAND ${GLSLC} ${SHADER_DIR}/frag.frag -o ${FRAG_SPV}
        DEPENDS ${SHADER_DIR}/frag.frag)
    add_custom_command(OUTPUT ${UI_VERT_SPV}
        COMMAND ${GLSLC} ${SHADER_DIR}/ui_vert.vert -o ${UI_VERT_SPV}
        DEPENDS ${SHADER_DIR}/ui_vert.vert)
    add_custom_command(OUTPUT ${UI_FRAG_SPV}
        COMMAND ${GLSLC} ${SHADER_DIR}/ui_frag.frag -o ${UI_FRAG_SPV}
        DEPENDS ${SHADER_DIR}/ui_frag.frag)

    add_custom_target(shaders DEPENDS ${VERT_SPV} ${FRAG_SPV} ${UI_VERT_SPV} ${UI_FRAG_SPV})
    add_dependencies(game shaders)

    add_custom_command(TARGET game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:game>/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VERT_SPV}    $<TARGET_FILE_DIR:game>/shaders/vert.spv
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FRAG_SPV}    $<TARGET_FILE_DIR:game>/shaders/frag.spv
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${UI_VERT_SPV} $<TARGET_FILE_DIR:game>/shaders/ui_vert.spv
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${UI_FRAG_SPV} $<TARGET_FILE_DIR:game>/shaders/ui_frag.spv
    )
endif()
