cmake_minimum_required(VERSION 3.20)
project(BgfxBuild LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "BgfxBuild currently supports Windows with MSVC-compatible compilers only.")
endif()
option(BGFX_MSVC_STATIC_RUNTIME "Link the static MSVC CRT" ON)
if(BGFX_MSVC_STATIC_RUNTIME)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set(CMAKE_DEBUG_POSTFIX d)
# Match upstream's NoExceptions without leaving CMake's /EHsc enabled.
string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

get_filename_component(BGFX_REPOS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../Repos/BgfxBuild" ABSOLUTE)
set(BX_DIR "${BGFX_REPOS_DIR}/bx")
set(BIMG_DIR "${BGFX_REPOS_DIR}/bimg")
set(BGFX_DIR "${BGFX_REPOS_DIR}/bgfx")
foreach(repo bx bimg bgfx)
  if(NOT EXISTS "${BGFX_REPOS_DIR}/${repo}/include/${repo}")
    message(FATAL_ERROR "Missing ${repo} repository in ${BGFX_REPOS_DIR}")
  endif()
endforeach()

file(GLOB BX_SOURCES CONFIGURE_DEPENDS "${BX_DIR}/src/*.cpp")
list(FILTER BX_SOURCES EXCLUDE REGEX "/(amalgamated|crtnone)\\.cpp$")
add_library(bx STATIC ${BX_SOURCES})
target_include_directories(bx
  PUBLIC
    "$<BUILD_INTERFACE:${BX_DIR}/include>"
    "$<BUILD_INTERFACE:${BX_DIR}/include/compat/msvc>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/compat/msvc>"
  PRIVATE "${BX_DIR}/3rdparty")
target_compile_definitions(bx PUBLIC "BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,0>")
target_link_libraries(bx PUBLIC psapi)

# Current bimg uses astc-encoder rather than the older astc-codec decoder.
file(GLOB ASTC_SOURCES CONFIGURE_DEPENDS "${BIMG_DIR}/3rdparty/astc-encoder/source/*.cpp")
add_library(bimg STATIC "${BIMG_DIR}/src/image.cpp" ${ASTC_SOURCES})
target_include_directories(bimg
  PUBLIC "$<BUILD_INTERFACE:${BIMG_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  PRIVATE "${BIMG_DIR}/3rdparty/astc-encoder/include")
target_compile_definitions(bimg PRIVATE ASTCENC_F16C=0 ASTCENC_NEON=0)
target_compile_options(bimg PRIVATE /fp:precise)
target_link_libraries(bimg PUBLIC bx)

# Platform guards in the sources select the Windows renderers.
file(GLOB BGFX_SOURCES CONFIGURE_DEPENDS "${BGFX_DIR}/src/*.cpp")
list(FILTER BGFX_SOURCES EXCLUDE REGEX "/amalgamated\\.cpp$")
add_library(bgfx STATIC ${BGFX_SOURCES})
target_include_directories(bgfx
  PUBLIC "$<BUILD_INTERFACE:${BGFX_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  PRIVATE "${BGFX_DIR}/3rdparty"
          "${BGFX_DIR}/3rdparty/directx-headers/include/directx"
          "${BGFX_DIR}/3rdparty/khronos")
target_link_libraries(bgfx PUBLIC bimg PRIVATE gdi32)

foreach(target bx bimg bgfx)
  add_library(bgfx::${target} ALIAS ${target})
  target_compile_features(${target} PUBLIC cxx_std_20)
  target_compile_options(${target} PUBLIC /Zc:__cplusplus /Zc:preprocessor
    PRIVATE /GR- /EHs-c- /utf-8 /wd4201 /wd4324)
  target_compile_definitions(${target} PRIVATE
    __STDC_LIMIT_MACROS __STDC_FORMAT_MACROS __STDC_CONSTANT_MACROS
    _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS _CRT_SECURE_NO_DEPRECATE
    _SCL_SECURE_NO_WARNINGS _SCL_SECURE=0 _SECURE_SCL=0)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
endforeach()

install(TARGETS bx bimg bgfx EXPORT bgfxTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "${BX_DIR}/include/bx" "${BIMG_DIR}/include/bimg" "${BGFX_DIR}/include/bgfx"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY "${BX_DIR}/include/compat/msvc"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/compat")
foreach(repo bx bimg bgfx)
  install(FILES "${BGFX_REPOS_DIR}/${repo}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${repo}")
endforeach()
install(FILES "${BIMG_DIR}/3rdparty/astc-encoder/LICENSE.txt"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/bimg/astc-encoder")
set(BGFX_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/bgfx")
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/bgfxConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/bgfxConfig.cmake"
  INSTALL_DESTINATION "${BGFX_INSTALL_CMAKEDIR}")
install(EXPORT bgfxTargets NAMESPACE bgfx:: DESTINATION "${BGFX_INSTALL_CMAKEDIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/bgfxConfig.cmake"
  DESTINATION "${BGFX_INSTALL_CMAKEDIR}")
