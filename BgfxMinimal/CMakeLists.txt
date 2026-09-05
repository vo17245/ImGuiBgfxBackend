cmake_minimum_required(VERSION 3.25)
project(BgfxMinimal LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "BgfxMinimal currently supports Windows/MSVC.")
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../Dependencies/Packages")
find_package(SDL3 CONFIG REQUIRED COMPONENTS SDL3-static)
find_package(bgfx CONFIG REQUIRED)

add_executable(BgfxMinimal main.cpp)
set_target_properties(BgfxMinimal PROPERTIES
  CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
target_compile_options(BgfxMinimal PRIVATE /W4 /utf-8 /permissive-)
target_compile_definitions(BgfxMinimal PRIVATE SDL_MAIN_HANDLED NOMINMAX)
target_link_libraries(BgfxMinimal PRIVATE SDL3::SDL3-static bgfx::bgfx)
