# Shared VST3 build for the plugins on the VST3 SDK directly (#171).
#
#   include(${CMAKE_CURRENT_SOURCE_DIR}/../common/vst3/LoudioVst3.cmake)
#   loudio_add_vst3_plugin(LoudioReverb_VST3
#       PACKAGE_NAME "Loudio Reverb"
#       SOURCES src/vst3_reverb.cpp src/ReverbCore.cpp
#       EDITOR_SOURCES src/ReverbUi.cpp)
#
# adds the vendored SDK (ThirdParty/vst3sdk, MIT) once, builds the plugin
# bundle with the SDK's own smtg_add_vst3plugin (bundle layout,
# moduleinfo.json, platform entry points), and a `<target>_validate`
# target that runs the SDK's validator (extensive mode) on the built bundle. EDITOR_SOURCES
# (the plugin's cuif UI) and the cuif Framework are compiled only where
# cuif has a window backend, which today is Windows (LOUDIO_VST3_EDITOR).

set(LOUDIO_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(LOUDIO_VST3SDK_DIR ${LOUDIO_ROOT_DIR}/ThirdParty/vst3sdk)

if(NOT TARGET sdk)
    # Only what the plugins and their CI need: no VSTGUI, no example
    # plug-ins, no symlinks into the user's VST3 folder; the hosting
    # examples stay on because they hold the validator. The validator is
    # run by <target>_validate rather than after every build.
    set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF)
    set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON)
    set(SMTG_ENABLE_VSTGUI_SUPPORT OFF)
    set(SMTG_RUN_VST_VALIDATOR OFF)
    set(SMTG_CREATE_PLUGIN_LINK OFF)
    set(SMTG_ENABLE_IOS_TARGETS OFF)
    set(vst3sdk_SOURCE_DIR ${LOUDIO_VST3SDK_DIR})
    add_subdirectory(${LOUDIO_VST3SDK_DIR} ${CMAKE_BINARY_DIR}/vst3sdk EXCLUDE_FROM_ALL)
endif()
# Not smtg_enable_vst3_sdk(): it also sets directory-wide compiler flags
# (/fp:fast on MSVC, -ffast-math under Xcode) and output directories,
# which would reach every target here -- the DSP tests and the LPI build
# included. The plugin target needs only the platform variables (SMTG_WIN
# etc., cached by the SDK's own configure) and where the SDK's module
# entry points live.
set(public_sdk_SOURCE_DIR ${LOUDIO_VST3SDK_DIR}/public.sdk)

# For test hosts that load a built bundle through VST3::Hosting::Module:
# the platform's module loader (which the SDK leaves to each host) and
# MemoryStream, as C++17 like the SDK itself -- module_win32.cpp does not
# compile as C++20 (u8path() returns a char8_t string there).
if(NOT TARGET loudio_vst3_hosting)
    if(APPLE)
        set(module_source ${LOUDIO_VST3SDK_DIR}/public.sdk/source/vst/hosting/module_mac.mm)
        set_source_files_properties(${module_source} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    elseif(WIN32)
        set(module_source ${LOUDIO_VST3SDK_DIR}/public.sdk/source/vst/hosting/module_win32.cpp)
    else()
        set(module_source ${LOUDIO_VST3SDK_DIR}/public.sdk/source/vst/hosting/module_linux.cpp)
    endif()
    add_library(loudio_vst3_hosting STATIC
        ${module_source}
        ${LOUDIO_VST3SDK_DIR}/public.sdk/source/common/memorystream.cpp
    )
    set_target_properties(loudio_vst3_hosting PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(loudio_vst3_hosting PUBLIC sdk_hosting)
    if(APPLE)
        find_library(LOUDIO_COCOA_FRAMEWORK Cocoa)
        target_link_libraries(loudio_vst3_hosting PUBLIC ${LOUDIO_COCOA_FRAMEWORK})
    endif()
endif()

function(loudio_add_vst3_plugin target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "PACKAGE_NAME" "SOURCES;EDITOR_SOURCES")

    set(sources ${ARG_SOURCES})
    if(WIN32)
        list(APPEND sources
            ${ARG_EDITOR_SOURCES}
            ${LOUDIO_ROOT_DIR}/Framework/src/window_win32.c
            ${LOUDIO_ROOT_DIR}/Framework/src/graphics.c
            ${LOUDIO_ROOT_DIR}/Framework/src/font.c
            ${LOUDIO_ROOT_DIR}/Framework/src/widget.c
            ${LOUDIO_ROOT_DIR}/Framework/src/theme.c
            ${LOUDIO_ROOT_DIR}/Framework/src/theme_hello_kitty.c
            ${LOUDIO_ROOT_DIR}/Framework/src/theme_greens.c
        )
    endif()

    smtg_add_vst3plugin(${target} PACKAGE_NAME "${ARG_PACKAGE_NAME}" SOURCES_LIST ${sources})
    target_link_libraries(${target} PRIVATE sdk)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${LOUDIO_ROOT_DIR}/VSTs/common
        ${LOUDIO_ROOT_DIR}/Framework/include
        ${LOUDIO_ROOT_DIR}/DSP/include
    )
    target_compile_features(${target} PRIVATE cxx_std_20)
    # Only the SDK's entry points (SMTG_EXPORT_SYMBOL) leave the module.
    set_target_properties(${target} PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_compile_definitions(${target} PRIVATE LOUDIO_PLUGIN_VERSION="${PROJECT_VERSION}")
    if(WIN32)
        target_compile_definitions(${target} PRIVATE LOUDIO_VST3_EDITOR UNICODE _UNICODE NOMINMAX)
        target_link_libraries(${target} PRIVATE opengl32 gdi32 user32)
    endif()

    # The SDK names the bundle "<PACKAGE_NAME>.vst3" under VST3/<config>/.
    get_target_property(package_path ${target} SMTG_PLUGIN_PACKAGE_PATH)
    set_target_properties(${target} PROPERTIES LOUDIO_VST3_BUNDLE "${package_path}")
    add_custom_target(${target}_validate
        COMMAND validator -e "${package_path}"
        DEPENDS ${target} validator
        COMMENT "VST3 validator: ${ARG_PACKAGE_NAME}"
        VERBATIM
    )
endfunction()
