# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
# Builds the LV2 bundle on any platform, the VST3 plugin (Windows, macOS, Linux) and the Audio
# Unit (macOS). build.bat (Windows, MSVC) and the Makefile build the LV2 bundle without CMake.
cmake_minimum_required(VERSION 3.16)
project(duo57 VERSION 1.2.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The extra formats download their SDKs while configuring (both permissively licensed; see the
# README). The VST3 SDK needs CMake 3.25 or later.
option(DUO_VST3 "Build the VST3 plugin (fetches the Steinberg VST3 SDK)" ON)
option(DUO_AU "Build the Audio Unit (macOS; fetches the Apple AudioUnitSDK)" ${APPLE})

if(WIN32)
    set(LIB_SUFFIX .dll)
    set(UI_CLASS WindowsUI)
    set(WITH_UI ON)
elseif(APPLE)
    set(LIB_SUFFIX .dylib)
    set(UI_CLASS CocoaUI)
    set(WITH_UI ON)
    enable_language(OBJC) # the Cocoa window layer in view.c
    set_source_files_properties(src/ui/view.c PROPERTIES LANGUAGE OBJC)
else()
    set(LIB_SUFFIX .so)
    set(UI_CLASS X11UI)
    find_package(X11)
    set(WITH_UI ${X11_FOUND})
endif()

set(BUNDLE_DIR ${CMAKE_BINARY_DIR}/duo57.lv2)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
if(NOT MSVC)
    # Every format runs the same arithmetic: no fused multiply-adds behind the code's back.
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX,OBJC>:-ffp-contract=off>)
endif()

add_library(duo57 MODULE src/duo57.c)
set_target_properties(duo57 PROPERTIES
    PREFIX ""
    SUFFIX ${LIB_SUFFIX}
    C_STANDARD 99
    C_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY "$<1:${BUNDLE_DIR}>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${BUNDLE_DIR}>")

if(WITH_UI)
    add_library(duo57_ui MODULE src/ui/ui_lv2.c src/ui/view.c)
    set_target_properties(duo57_ui PROPERTIES
        PREFIX ""
        SUFFIX ${LIB_SUFFIX}
        C_STANDARD 11
        C_VISIBILITY_PRESET hidden
        OBJC_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY "$<1:${BUNDLE_DIR}>"
        RUNTIME_OUTPUT_DIRECTORY "$<1:${BUNDLE_DIR}>")
    if(WIN32)
        target_link_libraries(duo57_ui PRIVATE user32 gdi32)
    elseif(APPLE)
        target_link_libraries(duo57_ui PRIVATE "-framework Cocoa")
    else()
        target_include_directories(duo57_ui PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(duo57_ui PRIVATE ${X11_LIBRARIES} m)
    endif()
endif()

add_executable(render test/render.c)
set_target_properties(render PROPERTIES C_STANDARD 99)
add_executable(ui_render test/ui_render.c)
set_target_properties(ui_render PROPERTIES C_STANDARD 11)
add_executable(params_check test/params_check.c)
set_target_properties(params_check PROPERTIES C_STANDARD 11)

if(NOT MSVC)
    target_compile_definitions(duo57 PRIVATE _DEFAULT_SOURCE)
    target_compile_definitions(render PRIVATE _DEFAULT_SOURCE)
    foreach(T duo57 render ui_render params_check)
        target_compile_options(${T} PRIVATE -Wall -Wextra)
        target_link_libraries(${T} PRIVATE m)
    endforeach()
    foreach(T ui_render params_check)
        # The tests use parts of the panel and parameter headers, so the rest goes unused there.
        target_compile_options(${T} PRIVATE -Wno-unused-function)
    endforeach()
endif()

# The manifest names the libraries and the GUI class for this platform.
file(READ duo57.lv2/manifest.ttl.in MANIFEST)
string(REPLACE "@LIB_EXT@" "${LIB_SUFFIX}" MANIFEST "${MANIFEST}")
string(REPLACE "@UI_CLASS@" "${UI_CLASS}" MANIFEST "${MANIFEST}")
if(NOT WITH_UI)
    string(REGEX REPLACE "# The build fills in.*" "" MANIFEST "${MANIFEST}")
endif()
file(WRITE ${BUNDLE_DIR}/manifest.ttl "${MANIFEST}")
configure_file(duo57.lv2/duo57.ttl ${BUNDLE_DIR}/duo57.ttl COPYONLY)
# The licences travel with every bundle.
set(DUO_DOCS ${CMAKE_SOURCE_DIR}/LICENSE ${CMAKE_SOURCE_DIR}/LICENSE-LV2)
foreach(DOC ${DUO_DOCS})
    configure_file(${DOC} ${BUNDLE_DIR}/ COPYONLY)
endforeach()

enable_testing()
add_test(NAME render COMMAND render WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME ui_render COMMAND ui_render ${CMAKE_BINARY_DIR}/panel.bmp)
add_test(NAME params_check COMMAND params_check ${CMAKE_SOURCE_DIR}/duo57.lv2/duo57.ttl)

# "cmake --install build-cmake --prefix ~/.lv2" style installs: the bundle goes in <prefix>/lib/lv2
# by default, or straight into the prefix with -DLV2_INSTALL_DIR=.
set(LV2_INSTALL_DIR lib/lv2 CACHE STRING "Folder under the install prefix that receives the bundle")
install(DIRECTORY ${BUNDLE_DIR} DESTINATION ${LV2_INSTALL_DIR})

include(FetchContent)

# ---- VST3: build-cmake/VST3/<config>/Davidian Duo 57.vst3 ----
# In its own folder, because the VST3 SDK sets compiler options (such as /fp:fast) for the whole
# folder that sets it up, and the LV2 build must not get them.
if(DUO_VST3)
    add_subdirectory(src/vst3)
endif()

# ---- Audio Unit: build-cmake/AU/Davidian Duo 57.component ----
if(DUO_AU)
    if(NOT APPLE)
        message(FATAL_ERROR "Audio Units are macOS only")
    endif()
    FetchContent_Declare(audiounitsdk
        GIT_REPOSITORY https://github.com/apple/AudioUnitSDK.git
        GIT_TAG AudioUnitSDK-1.2.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(audiounitsdk) # sources only; it has no CMake build
    set(AUSDK ${audiounitsdk_SOURCE_DIR})
    set(AUSDK_SOURCES)
    foreach(S AUBase AUBuffer AUBufferAllocator AUEffectBase AUInputElement AUOutputElement
              AUPlugInDispatch AUScopeElement ComponentBase)
        list(APPEND AUSDK_SOURCES ${AUSDK}/src/AudioUnitSDK/${S}.cpp)
    endforeach()

    # Names and four-character codes. Changing them breaks saved sessions.
    set(DUO_AU_NAME "Davidian Duo 57")
    set(DUO_AU_BUNDLE_ID "com.davidian.duo-57.au")
    set(DUO_AU_MANUFACTURER "Dvdn")
    set(DUO_AU_SUBTYPE "Du57")
    set(DUO_AU_FACTORY "DavidianDuo57AUFactory") # from AUSDK_COMPONENT_ENTRY in au_plugin.cpp
    math(EXPR DUO_AU_VERSION_INT "${PROJECT_VERSION_MAJOR} * 65536 + ${PROJECT_VERSION_MINOR} * 256 + ${PROJECT_VERSION_PATCH}")
    configure_file(src/au/Info.plist.in ${CMAKE_BINARY_DIR}/au_plist/Info.plist @ONLY)

    add_library(duo57_au MODULE src/au/au_plugin.cpp src/au/au_view.m src/duo57.c src/ui/view.c
        ${AUSDK_SOURCES})
    set_target_properties(duo57_au PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION component
        OUTPUT_NAME "${DUO_AU_NAME}"
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/au_plist/Info.plist
        C_STANDARD 11
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        OBJC_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/AU>")
    target_include_directories(duo57_au PRIVATE ${AUSDK}/include)
    target_compile_definitions(duo57_au PRIVATE DUO_AU_BUNDLE_ID="${DUO_AU_BUNDLE_ID}" DUO57_NO_LV2_ENTRY
        DUO_AU_VIEW_FACTORY=DavidianDuo57AUViewFactory DUO_AU_CONTAINER=DavidianDuo57AUContainer
        DUO_VIEW_CLASS=DavidianDuo57AUPanel)
    target_compile_options(duo57_au PRIVATE $<$<COMPILE_LANGUAGE:C,OBJC>:-Wall -Wextra -Wno-unused-function>)
    target_link_libraries(duo57_au PRIVATE "-framework AudioToolbox" "-framework AudioUnit"
        "-framework CoreAudio" "-framework CoreFoundation" "-framework Cocoa")
    add_custom_command(TARGET duo57_au POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:duo57_au>/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy ${DUO_DOCS} ${CMAKE_SOURCE_DIR}/licenses/Apache-2.0.txt
            "$<TARGET_BUNDLE_CONTENT_DIR:duo57_au>/Resources/"
        # An ad hoc signature: Apple silicon will not load unsigned code.
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:duo57_au>"
        VERBATIM)
endif()
