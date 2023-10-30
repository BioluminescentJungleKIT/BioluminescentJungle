cmake_minimum_required(VERSION 3.26)
project(Jungle)

set(CMAKE_CXX_STANDARD 20)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

include_directories(lib/tinygltf)
add_subdirectory(lib/tinygltf)
include_directories(Jungle PRIVATE lib/imgui lib/imgui/backends lib/imgui/misc/cpp/)

add_executable(Jungle src/main.cpp
        src/JungleApp.cpp
        src/JungleApp.h
        lib/imgui/imconfig.h
        lib/imgui/imgui.cpp
        lib/imgui/imgui.h
        lib/imgui/imgui_demo.cpp
        lib/imgui/imgui_draw.cpp
        lib/imgui/imgui_internal.h
        lib/imgui/imgui_tables.cpp
        lib/imgui/imgui_widgets.cpp
        lib/imgui/imstb_rectpack.h
        lib/imgui/imstb_textedit.h
        lib/imgui/imstb_truetype.h
        lib/imgui/backends/imgui_impl_glfw.cpp
        lib/imgui/backends/imgui_impl_glfw.h
        lib/imgui/backends/imgui_impl_vulkan.cpp
        lib/imgui/backends/imgui_impl_vulkan.h
        lib/imgui/misc/cpp/imgui_stdlib.cpp
        lib/imgui/misc/cpp/imgui_stdlib.h
        src/Scene.cpp
        src/Scene.h
        src/VulkanHelper.h
        src/VulkanHelper.cpp
)

target_link_libraries(Jungle glfw vulkan shaderc_combined tinygltf)