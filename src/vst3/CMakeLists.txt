# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
# The VST3 plugin, built with the Steinberg VST3 SDK (MIT licence), fetched at configure time.
# Output: <build>/VST3/<config>/Davidian Duo 57.vst3
cmake_minimum_required(VERSION 3.25)

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE) # builds the validator
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)  # no links into the system VST3 folder
set(SMTG_USE_STATIC_CRT ON CACHE BOOL "" FORCE)       # no runtime redistributable needed
set(SMTG_RUN_VST_VALIDATOR ON CACHE BOOL "" FORCE)    # the SDK validator runs after each build
FetchContent_Declare(vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG v3.8.1_build_84
    GIT_SHALLOW TRUE
    GIT_SUBMODULES base cmake pluginterfaces public.sdk)
FetchContent_MakeAvailable(vst3sdk)
smtg_enable_vst3_sdk()

# The amp checks for NaN, which fast floating-point math may skip, and should sound the same as
# the LV2 build.
get_directory_property(DUO_OPTIONS COMPILE_OPTIONS)
list(REMOVE_ITEM DUO_OPTIONS /fp:fast -ffast-math -ffp-contract=fast)
set_directory_properties(PROPERTIES COMPILE_OPTIONS "${DUO_OPTIONS}")

smtg_add_vst3plugin(duo57_vst3 PACKAGE_NAME "Davidian Duo 57"
    SOURCES_LIST vst3_plugin.cpp ../duo57.c ../ui/view.c)
target_compile_features(duo57_vst3 PRIVATE cxx_std_17)
target_compile_definitions(duo57_vst3 PRIVATE DUO_VERSION="${PROJECT_VERSION}" DUO57_NO_LV2_ENTRY
    DUO_WINDOW_CLASS="DavidianDuo57VST3" DUO_VIEW_CLASS=DavidianDuo57VST3Panel)
set_target_properties(duo57_vst3 PROPERTIES C_STANDARD 11)
target_link_libraries(duo57_vst3 PRIVATE sdk)
if(WIN32)
    target_link_libraries(duo57_vst3 PRIVATE user32 gdi32)
elseif(APPLE)
    set_source_files_properties(../ui/view.c PROPERTIES LANGUAGE OBJC)
    target_link_libraries(duo57_vst3 PRIVATE "-framework Cocoa")
else()
    find_package(X11 REQUIRED) # the editor's X11 window layer
    target_include_directories(duo57_vst3 PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(duo57_vst3 PRIVATE ${X11_LIBRARIES} m)
endif()

# The library sits in Contents/<arch>; the licences go in Contents/Resources. (The SDK's own
# resource helper does not quote spaces in the bundle name.)
add_custom_command(TARGET duo57_vst3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:duo57_vst3>/../Resources"
    COMMAND ${CMAKE_COMMAND} -E copy ${DUO_DOCS} ${CMAKE_SOURCE_DIR}/licenses/VST3-SDK-MIT.txt
        "$<TARGET_FILE_DIR:duo57_vst3>/../Resources/"
    # The SDK gives Windows bundles its own logo as a folder icon; that logo is not ours to ship.
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:duo57_vst3>/../../PlugIn.ico"
        "$<TARGET_FILE_DIR:duo57_vst3>/../../desktop.ini"
    VERBATIM)

# A small Windows host that checks the editor and compares the sound with the LV2 build.
if(WIN32)
    add_executable(vst3_check ../../test/vst3_check.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp)
    target_compile_features(vst3_check PRIVATE cxx_std_17)
    target_compile_definitions(vst3_check PRIVATE _USE_MATH_DEFINES)
    target_link_libraries(vst3_check PRIVATE sdk_hosting user32)
    add_dependencies(vst3_check duo57_vst3 duo57)
    add_test(NAME vst3_check
        COMMAND vst3_check "${CMAKE_BINARY_DIR}/VST3/$<CONFIG>/Davidian Duo 57.vst3" "${BUNDLE_DIR}")
endif()
