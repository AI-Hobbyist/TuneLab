cmake_minimum_required(VERSION 3.20)

project(svs_core VERSION 0.1.0 LANGUAGES CXX)

option(SVS_CORE_BUILD_TESTS "Build the SVS Core console smoke test" ON)
option(SVS_CORE_ENABLE_DOTNET_HOSTING "Build the optional .NET hosting module" OFF)
option(SVS_CORE_BUILD_QT_DEMO "Build the optional Qt piano-roll test application" OFF)

set(SVS_QT_ROOT "C:/Qt/6.10.3/msvc2022_64" CACHE PATH
    "Qt installation used by the optional SVS Core piano-roll test application")

set(SVS_CORE_DEPLOY_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH
    "Directory containing the deployable SVS_Core runtime")
set(SVS_CORE_OUTPUT_DIR "${SVS_CORE_DEPLOY_DIR}" CACHE PATH
    "Directory for all SVS Core runtime binaries")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(svs_core_set_output target)
    set_target_properties(${target} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${SVS_CORE_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${SVS_CORE_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${SVS_CORE_OUTPUT_DIR}")
endfunction()

add_library(svs_core_g2p SHARED src/module_g2p.cpp)
target_include_directories(svs_core_g2p PRIVATE include src)
target_compile_definitions(svs_core_g2p PRIVATE SVS_MODULE_EXPORTS)
svs_core_set_output(svs_core_g2p)

add_library(svs_core_layout SHARED src/module_layout.cpp)
target_include_directories(svs_core_layout PRIVATE include src)
target_compile_definitions(svs_core_layout PRIVATE SVS_MODULE_EXPORTS)
svs_core_set_output(svs_core_layout)

add_library(svs_core SHARED src/core.cpp)
target_include_directories(svs_core PUBLIC include PRIVATE src)
target_compile_definitions(svs_core PRIVATE SVS_CORE_EXPORTS)
if(SVS_CORE_ENABLE_DOTNET_HOSTING)
    set(SVS_DOTNET_HOST_PACK "C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Host.win-x64/10.0.9/runtimes/win-x64/native"
        CACHE PATH "Directory containing nethost headers and library")
    target_compile_definitions(svs_core PRIVATE SVS_CORE_ENABLE_DOTNET_HOSTING)
    target_include_directories(svs_core PRIVATE "${SVS_DOTNET_HOST_PACK}")
    target_link_libraries(svs_core PRIVATE "${SVS_DOTNET_HOST_PACK}/nethost.lib")
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(svs_core PRIVATE dl)
endif()
svs_core_set_output(svs_core)

add_library(svs_native_test_engine SHARED src/native_test_engine.cpp)
target_include_directories(svs_native_test_engine PRIVATE include)
set_target_properties(svs_native_test_engine PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${SVS_CORE_OUTPUT_DIR}/Engines/svs-native-test"
    LIBRARY_OUTPUT_DIRECTORY "${SVS_CORE_OUTPUT_DIR}/Engines/svs-native-test"
    RUNTIME_OUTPUT_DIRECTORY "${SVS_CORE_OUTPUT_DIR}/Engines/svs-native-test")
add_custom_command(TARGET svs_native_test_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../tests/engines/Native.Sample/manifest.json"
        "${SVS_CORE_OUTPUT_DIR}/Engines/svs-native-test/manifest.json")

if(SVS_CORE_ENABLE_DOTNET_HOSTING)
    find_program(SVS_DOTNET_EXECUTABLE dotnet REQUIRED)
    add_custom_target(svs_dotnet_test_engine ALL
        COMMAND "${SVS_DOTNET_EXECUTABLE}" build
            "${CMAKE_CURRENT_SOURCE_DIR}/../tests/engines/DotNet.Sample/SvsCore.TestPlugin.csproj"
            --configuration Debug --output "${SVS_CORE_OUTPUT_DIR}/Engines/svs-dotnet-test"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../tests/engines/DotNet.Sample/manifest.json"
            "${SVS_CORE_OUTPUT_DIR}/Engines/svs-dotnet-test/manifest.json"
        VERBATIM)
endif()

add_custom_command(TARGET svs_core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SVS_CORE_OUTPUT_DIR}/Engines"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SVS_CORE_OUTPUT_DIR}/include"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/include/svs_core.h"
        "${SVS_CORE_OUTPUT_DIR}/include/svs_core.h"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/include/svs_plugin.h"
        "${SVS_CORE_OUTPUT_DIR}/include/svs_plugin.h")
if(SVS_CORE_ENABLE_DOTNET_HOSTING)
    add_custom_command(TARGET svs_core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SVS_DOTNET_HOST_PACK}/nethost.dll"
            "${SVS_CORE_OUTPUT_DIR}/nethost.dll")
endif()

if(SVS_CORE_BUILD_TESTS)
    add_executable(svs_testexe ../tests/svs_testexe/main.cpp)
    target_link_libraries(svs_testexe PRIVATE svs_core)
    target_include_directories(svs_testexe PRIVATE include)
    svs_core_set_output(svs_testexe)
    add_dependencies(svs_testexe svs_core_g2p svs_core_layout svs_native_test_engine)
    if(SVS_CORE_ENABLE_DOTNET_HOSTING)
        add_dependencies(svs_testexe svs_dotnet_test_engine)
    endif()

    enable_testing()
    add_test(NAME svs_m1_smoke COMMAND svs_testexe)
    set_tests_properties(svs_m1_smoke PROPERTIES WORKING_DIRECTORY "${SVS_CORE_OUTPUT_DIR}")
endif()

if(SVS_CORE_BUILD_QT_DEMO)
    list(PREPEND CMAKE_PREFIX_PATH "${SVS_QT_ROOT}")
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../tests/QtSvsPianoRoll"
        "${CMAKE_CURRENT_BINARY_DIR}/QtSvsPianoRoll")
endif()