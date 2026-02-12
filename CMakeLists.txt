cmake_minimum_required(VERSION 3.24)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(AsymHorrorPrototype LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable GLM experimental extensions
add_compile_definitions(GLM_ENABLE_EXPERIMENTAL)

option(BUILD_IMGUI "Build Dear ImGui for debug UI/console" ON)
option(USE_GLFW_STATIC "Build GLFW as a static library" ON)

include(FetchContent)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

if(USE_GLFW_STATIC)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
else()
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)

FetchContent_Declare(
    enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG v1.3.17
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.5
)

set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_GL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_VALIDATOR_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_BUILDER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_HEADER_ONLY ON CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw glm enet nlohmann_json tinygltf)

find_package(OpenGL REQUIRED)

set(ENGINE_SOURCES
    src/main.cpp
    external/glad/src/glad.c
    engine/audio/AudioSystem.cpp
    engine/core/App.cpp
    engine/core/EventBus.cpp
    engine/core/Time.cpp
    engine/assets/AssetRegistry.cpp
    engine/assets/MeshLibrary.cpp
    engine/fx/FxSystem.cpp
    engine/platform/Window.cpp
    engine/platform/Input.cpp
    engine/platform/ActionBindings.cpp
    engine/net/NetworkSession.cpp
    engine/net/LanDiscovery.cpp
    engine/render/Renderer.cpp
    engine/physics/PhysicsWorld.cpp
    engine/scene/World.cpp
    engine/ui/UiSystem.cpp
    game/ui/LoadingScreen.cpp
    game/ui/LoadingManager.cpp
    game/maps/TileGenerator.cpp
    game/gameplay/GameplaySystems.cpp
    game/gameplay/SpawnSystem.cpp
    game/gameplay/PerkSystem.cpp
    game/gameplay/LoadoutSystem.cpp
    game/editor/LevelAssets.cpp
    game/editor/LevelEditor.cpp
    ui/DeveloperConsole.cpp
)

add_executable(asym_horror ${ENGINE_SOURCES})

target_include_directories(asym_horror PRIVATE
    .
    external/glad/include
    external/miniaudio
    external/stb
    ${enet_SOURCE_DIR}/include
    ${tinygltf_SOURCE_DIR}
)

target_link_libraries(asym_horror PRIVATE
    glfw
    OpenGL::GL
    glm::glm
    nlohmann_json::nlohmann_json
    enet
)

if(MSVC)
    target_compile_options(asym_horror PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
else()
    target_compile_options(asym_horror PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(asym_horror PRIVATE ws2_32 winmm comdlg32)
endif()

if(UNIX AND NOT APPLE)
    find_library(ASOUND_LIBRARY asound)
    if(ASOUND_LIBRARY)
        target_link_libraries(asym_horror PRIVATE ${ASOUND_LIBRARY})
    endif()
    target_link_libraries(asym_horror PRIVATE ${CMAKE_DL_LIBS} m pthread)
endif()

if(MINGW)
    # Ensure GCC runtime DLLs are bundled next to the executable.
    get_filename_component(MINGW_COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(MINGW_RUNTIME_DLL_CANDIDATES
        libstdc++-6.dll
        libwinpthread-1.dll
        libgcc_s_seh-1.dll
        libgcc_s_dw2-1.dll
        libgcc_s_sjlj-1.dll
    )

    foreach(RUNTIME_DLL IN LISTS MINGW_RUNTIME_DLL_CANDIDATES)
        if(EXISTS "${MINGW_COMPILER_BIN_DIR}/${RUNTIME_DLL}")
            add_custom_command(TARGET asym_horror POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_COMPILER_BIN_DIR}/${RUNTIME_DLL}"
                    $<TARGET_FILE_DIR:asym_horror>
            )
        endif()
    endforeach()
endif()

if(BUILD_IMGUI)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.9b
    )
    FetchContent_MakeAvailable(imgui)

    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )

    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)

    target_include_directories(imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        external/glad/include
    )

    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
    target_compile_definitions(asym_horror PRIVATE BUILD_WITH_IMGUI=1)
    target_link_libraries(asym_horror PRIVATE imgui)
else()
    target_compile_definitions(asym_horror PRIVATE BUILD_WITH_IMGUI=0)
endif()
