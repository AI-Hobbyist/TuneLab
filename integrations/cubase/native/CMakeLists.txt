cmake_minimum_required(VERSION 3.25)
project(TuneLabCubaseProbe VERSION 0.1.0 LANGUAGES C CXX)

set(DOTNET_HOST_INCLUDE "" CACHE PATH "Directory containing hostfxr.h and coreclr_delegates.h")
if(NOT EXISTS "${DOTNET_HOST_INCLUDE}/hostfxr.h")
    message(FATAL_ERROR "Set DOTNET_HOST_INCLUDE to the installed .NET host pack native directory")
endif()

add_executable(tunelab_host_probe host_probe.cpp)
target_compile_features(tunelab_host_probe PRIVATE cxx_std_17)
target_include_directories(tunelab_host_probe PRIVATE "${DOTNET_HOST_INCLUDE}")

set(VST3_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/vst3sdk" CACHE PATH "VST3 SDK")
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
add_subdirectory("${VST3_SDK_ROOT}" vst3sdk)
smtg_add_vst3plugin(TuneLabInstrumentProbe instrument_probe.cpp)
target_link_libraries(TuneLabInstrumentProbe PRIVATE sdk)
target_compile_features(TuneLabInstrumentProbe PRIVATE cxx_std_17)

set(ARA_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/ARA_SDK" CACHE PATH "ARA SDK")
add_subdirectory("${ARA_SDK_ROOT}/ARA_Library" ara_library)
smtg_add_vst3plugin(TuneLabBridgeProbe bridge_probe.cpp ara_document.h)
target_include_directories(TuneLabBridgeProbe PRIVATE "${ARA_SDK_ROOT}")
target_link_libraries(TuneLabBridgeProbe PRIVATE sdk ARA_PlugIn_Library)
target_compile_features(TuneLabBridgeProbe PRIVATE cxx_std_17)

foreach(plugin_target TuneLabInstrumentProbe TuneLabBridgeProbe)
    target_sources(${plugin_target} PRIVATE
        "${VST3_SDK_ROOT}/public.sdk/source/main/dllmain.cpp"
        module_lifecycle.cpp
    )
endforeach()

add_executable(tunelab_state_probe state_probe.cpp
    "${VST3_SDK_ROOT}/public.sdk/source/common/memorystream.cpp")
target_link_libraries(tunelab_state_probe PRIVATE sdk)
target_compile_features(tunelab_state_probe PRIVATE cxx_std_17)
