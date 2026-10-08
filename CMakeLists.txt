cmake_minimum_required(VERSION 3.29)

project(RandomSetPiece VERSION 0.4.3 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RANDOM_SET_PIECE_LOCAL_SDK_DIR "" CACHE PATH
    "Optional PluginSDK include tree for offline development builds")
if(RANDOM_SET_PIECE_LOCAL_SDK_DIR)
    add_library(D2RLPlugin INTERFACE)
    add_library(D2RLPlugin::D2RLPlugin ALIAS D2RLPlugin)
    target_include_directories(D2RLPlugin INTERFACE
        "${RANDOM_SET_PIECE_LOCAL_SDK_DIR}/include")
else()
    include(FetchContent)
    set(D2RLPLUGIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(D2RLPLUGIN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        D2RLoaderPluginSDK
        GIT_REPOSITORY https://github.com/D2RLoader/PluginSDK.git
        GIT_TAG 6eb8f8b6192868214706bd6d528c5294f2f551b7
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(D2RLoaderPluginSDK)
endif()

add_library(random_set_piece SHARED
    src/plugin.cpp
    src/plugin.rc
)
target_link_libraries(random_set_piece PRIVATE D2RLPlugin::D2RLPlugin)
target_compile_definitions(random_set_piece PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
set_target_properties(random_set_piece PROPERTIES
    OUTPUT_NAME "d2rl-random-set-piece"
    PREFIX ""
)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/config/random-set-piece.toml"
    default_config_text)
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${generated_dir}")
file(WRITE "${generated_dir}/default_config.hpp"
    "#pragma once\ninline constexpr char DefaultConfig[] = R\"D2RLCONFIG(${default_config_text})D2RLCONFIG\";\n")
target_include_directories(random_set_piece PRIVATE "${generated_dir}")

if(MSVC)
    target_compile_options(random_set_piece PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_link_options(random_set_piece PRIVATE /INCREMENTAL:NO /Brepro)
endif()
