add_executable(svs_qt_piano_roll WIN32 main.cpp)
target_compile_features(svs_qt_piano_roll PRIVATE cxx_std_17)
target_include_directories(svs_qt_piano_roll PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../SVS_Core/include")
target_link_libraries(svs_qt_piano_roll PRIVATE Qt6::Widgets svs_core)
set_target_properties(svs_qt_piano_roll PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${SVS_CORE_OUTPUT_DIR}")
add_dependencies(svs_qt_piano_roll svs_native_test_engine)
if(SVS_CORE_ENABLE_DOTNET_HOSTING)
    add_dependencies(svs_qt_piano_roll svs_dotnet_test_engine)
endif()

if(WIN32)
    find_program(SVS_QT_DEPLOY_TOOL windeployqt
        HINTS "${SVS_QT_ROOT}/bin" REQUIRED)
    add_custom_command(TARGET svs_qt_piano_roll POST_BUILD
        COMMAND "${SVS_QT_DEPLOY_TOOL}"
            --no-translations
            --no-system-d3d-compiler
            "$<TARGET_FILE:svs_qt_piano_roll>"
        VERBATIM)
endif()