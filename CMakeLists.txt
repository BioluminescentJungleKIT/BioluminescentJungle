cmake_minimum_required(VERSION 3.26)
project(Jungle)

set(CMAKE_CXX_STANDARD 20)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_executable(Jungle src/main.cpp
        src/JungleApp.cpp
        src/JungleApp.h
)

target_link_libraries(Jungle glfw vulkan shaderc_combined)