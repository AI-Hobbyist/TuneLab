cmake_minimum_required(VERSION 3.24)

project(Bridge_VST3 VERSION 0.1.0 LANGUAGES C CXX)

# 指向仓库内已 vendor 的 JUCE（JUCE 9 自带 VST3 包装所需的 VST3 SDK；
# THIRD_PARTY/vst3sdk 仅作协议对拍参考，不另行指定，避免与自带副本头文件冲突）
set(JUCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../THIRD_PARTY/JUCE")
add_subdirectory("${JUCE_ROOT}" juce)

# M0 仅 Windows（命名文件映射，见 BridgeVST3Shared.cpp）；POSIX shm 待后续里程碑。
if(NOT WIN32)
    message(FATAL_ERROR "Bridge_VST3 M0 仅支持 Windows")
endif()

juce_add_plugin(Bridge_VST3
    VENDOR_NAME "TuneLab"
    COMPANY_NAME "TuneLab"
    PRODUCT_NAME "Bridge_VST3"
    FORMATS VST3
    PLUGIN_MANUFACTURER_CODE TnLb
    PLUGIN_CODE TnBd
    VST3_CATEGORIES Instrument Synth
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build/Bridge_VST3"
)

target_sources(Bridge_VST3 PRIVATE
    Source/BridgeVST3Processor.cpp
    Source/BridgeVST3Editor.cpp
    Source/BridgeVST3Shared.cpp
)

target_include_directories(Bridge_VST3 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Bridge/protocol"
)

# 只发 VST3（不发 VST2）：关闭 JUCE 的 VST2/VST3 参数 ID 冲突守卫（见 juce_audio_plugin_client_VST3.cpp）。
target_compile_definitions(Bridge_VST3 PRIVATE
    JUCE_VST3_CAN_REPLACE_VST2=0
)

# 源码含中文注释：强制 UTF-8 解析，避免 MSVC 默认代码页 936 的 C4819 告警/乱码。
if(MSVC)
    target_compile_options(Bridge_VST3 PRIVATE /utf-8)
endif()
