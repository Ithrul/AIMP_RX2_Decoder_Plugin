cmake_minimum_required(VERSION 3.20)
project(aimp_rx2_plugin LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Plugin version (kept in sync with plugin.cpp/version.rc) for packaging names
set(RX2_VERSION "0.9.6" CACHE STRING "AIMP RX2 plugin version for packaging")

# Architecture-aware output and packaging directories
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(RX2_ARCH "x64")
else()
    set(RX2_ARCH "x86")
endif()

set(RX2_BIN_DIR     "${CMAKE_SOURCE_DIR}/out/bin/${RX2_ARCH}")
set(RX2_LIB_DIR     "${CMAKE_SOURCE_DIR}/out/lib/${RX2_ARCH}")
set(RX2_PDB_DIR     "${CMAKE_SOURCE_DIR}/out/symbols/${RX2_ARCH}")
set(RX2_PACKAGE_ROOT "${CMAKE_SOURCE_DIR}/out/package/aimp_rx2_plugin")
if(RX2_ARCH STREQUAL "x64")
    set(RX2_PACKAGE_STAGE "${RX2_PACKAGE_ROOT}/x64")
else()
    set(RX2_PACKAGE_STAGE "${RX2_PACKAGE_ROOT}")
endif()
set(RX2_PACKAGE_ZIP  "${CMAKE_SOURCE_DIR}/out/package/aimp_rx2_plugin_v${RX2_VERSION}.zip")

# Paths to SDKs
set(AIMP_SDK_DIR "${CMAKE_SOURCE_DIR}/external/AIMP_SDK")
set(REX_SDK_DIR  "${CMAKE_SOURCE_DIR}/external/REX_SDK")

# User-configurable AIMP install roots for deployment
set(AIMP_X64_DIR "C:/Program Files/AIMP" CACHE PATH "Path to 64-bit AIMP installation")
set(AIMP_X86_DIR "C:/Program Files (x86)/AIMP" CACHE PATH "Path to 32-bit AIMP installation")

if(RX2_ARCH STREQUAL "x64")
    set(REX_DLL_PATH "${REX_SDK_DIR}/Win/x64/Deployment/REX Shared Library.dll")
    set(RX2_AIMP_ROOT "${AIMP_X64_DIR}")
else()
    set(REX_DLL_PATH "${REX_SDK_DIR}/Win/x86/Deployment/REX Shared Library.dll")
    set(RX2_AIMP_ROOT "${AIMP_X86_DIR}")
endif()

set(REX_DLL_FOUND ON)
if(NOT EXISTS "${REX_DLL_PATH}")
    set(REX_DLL_FOUND OFF)
    message(WARNING "REX Shared Library not found at ${REX_DLL_PATH}; packaging copy will be skipped.")
endif()

# Accept minimum REX version per-arch (x64 needs >=1.7; x86 is stuck on legacy 1.6).
set(REX_MIN_VERSION_COMBINED 1007) # 1.7
set(REX_MIN_VERSION_HUMAN "1.7")
if(RX2_ARCH STREQUAL "x86")
    set(REX_MIN_VERSION_COMBINED 1006) # 1.6 legacy only
    set(REX_MIN_VERSION_HUMAN "1.6 (legacy)")
endif()

# Fail fast if the bundled REX Shared Library is too old for the SDK loader.
if(REX_DLL_FOUND)
    set(REX_DLL_VERSION_RAW "")
    execute_process(
        COMMAND powershell -NoLogo -NoProfile -Command
                "(Get-Item '${REX_DLL_PATH}').VersionInfo.FileVersion"
        OUTPUT_VARIABLE REX_DLL_VERSION_RAW
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE REX_DLL_VERSION_STATUS
    )
    if(REX_DLL_VERSION_STATUS EQUAL 0)
        message(STATUS "Detected REX Shared Library version: ${REX_DLL_VERSION_RAW}")
        string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" REX_DLL_VERSION_MATCH "${REX_DLL_VERSION_RAW}")
        if(REX_DLL_VERSION_MATCH)
            set(REX_DLL_VERSION_MAJOR "${CMAKE_MATCH_1}")
            set(REX_DLL_VERSION_MINOR "${CMAKE_MATCH_2}")
            math(EXPR REX_DLL_VERSION_COMBINED "${REX_DLL_VERSION_MAJOR}*1000 + ${REX_DLL_VERSION_MINOR}")
            if(REX_DLL_VERSION_COMBINED LESS REX_MIN_VERSION_COMBINED)
                message(FATAL_ERROR "REX Shared Library at ${REX_DLL_PATH} is too old (${REX_DLL_VERSION_RAW}); need >= ${REX_MIN_VERSION_HUMAN} for ${RX2_ARCH} builds.")
            endif()
            if(RX2_ARCH STREQUAL "x86" AND REX_DLL_VERSION_COMBINED LESS 1007)
                message(WARNING "Using legacy 32-bit REX Shared Library ${REX_DLL_VERSION_RAW}; Reason does not publish 1.7+ for x86. Expect limited support compared to x64.")
            endif()
        else()
            message(WARNING "Unable to parse REX Shared Library version from '${REX_DLL_VERSION_RAW}' at ${REX_DLL_PATH}")
        endif()
    else()
        message(WARNING "Unable to read REX Shared Library version at ${REX_DLL_PATH} (status ${REX_DLL_VERSION_STATUS}); version check skipped.")
    endif()
endif()

include_directories(
    "${AIMP_SDK_DIR}/Sources/Cpp"
    "${REX_SDK_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
)

if(RX2_ARCH STREQUAL "x86")
    # Use custom legacy loader for 32-bit builds (official REX x86 is stuck at v1.6).
    set(RX2_REX_LOADER_SRC src/RexLegacyLoader.cpp)
else()
    set(RX2_REX_LOADER_SRC external/REX_SDK/REX.c)
endif()

add_library(aimp_rx2_plugin SHARED
    src/plugin.cpp
    src/Rx2Decoder.cpp
    src/Rx2DecoderExtension.cpp
    src/Rx2FileFormatExtension.cpp
    src/version.rc
    src/Rx2Decoder.h
    src/Rx2DecoderExtension.h
    src/Rx2FileFormatExtension.h
    src/RexSdk.h
    ${RX2_REX_LOADER_SRC}
)

# Force REX.c to compile as C (x64 builds)
if(RX2_ARCH STREQUAL "x64")
    set_source_files_properties(
        external/REX_SDK/REX.c
        PROPERTIES LANGUAGE C
    )
endif()

target_compile_definitions(aimp_rx2_plugin PRIVATE
    REX_WINDOWS=1
    REX_DLL_LOADER=1
)

# Link against Windows Version.lib for GetFileVersionInfo* / VerQueryValue*
target_link_libraries(aimp_rx2_plugin PRIVATE
    Version
)

set_target_properties(aimp_rx2_plugin PROPERTIES
    OUTPUT_NAME "aimp_rx2_plugin"
    RUNTIME_OUTPUT_DIRECTORY "${RX2_BIN_DIR}/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${RX2_LIB_DIR}/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${RX2_LIB_DIR}/$<CONFIG>"
    PDB_OUTPUT_DIRECTORY "${RX2_PDB_DIR}/$<CONFIG>"
)

# Export undecorated plugin entry so AIMP finds it on both x86 and x64 builds.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_link_options(aimp_rx2_plugin PRIVATE "/EXPORT:AIMPPluginGetHeader=_AIMPPluginGetHeader@4")
else()
    target_link_options(aimp_rx2_plugin PRIVATE "/EXPORT:AIMPPluginGetHeader")
endif()

# Stage the built DLL into an AIMP-ready package layout per architecture
set(RX2_PACKAGE_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RX2_PACKAGE_STAGE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:aimp_rx2_plugin>"
            "${RX2_PACKAGE_STAGE}/aimp_rx2_plugin.dll"
)
if(REX_DLL_FOUND)
    list(APPEND RX2_PACKAGE_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${REX_DLL_PATH}"
                "${RX2_PACKAGE_STAGE}/REX Shared Library.dll"
    )
endif()
list(APPEND RX2_PACKAGE_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_SOURCE_DIR}/out/package"
            ${CMAKE_COMMAND} -E tar "cf" "${RX2_PACKAGE_ZIP}" --format=zip "aimp_rx2_plugin"
)

add_custom_command(TARGET aimp_rx2_plugin POST_BUILD
    ${RX2_PACKAGE_COMMANDS}
    COMMENT "Staging ${RX2_ARCH} plugin and updating installable zip"
    VERBATIM
)

#[[
# Optional post-build deployment helpers (disabled by default).
# These call the helper scripts in tools/ to copy the DLL into an AIMP install
# and restart the player for rapid testing. Enable and check respective paths in scripts if you want that workflow:
#   tools/deploy_and_restart_aimp.bat  <dll> <aimp_root> <arch>
#   tools/make_plugin_zip.ps1          (zip helper)
#add_custom_command(TARGET aimp_rx2_plugin POST_BUILD
#    COMMAND "${CMAKE_SOURCE_DIR}/tools/deploy_and_restart_aimp.bat"
#            "${RX2_PACKAGE_STAGE}/aimp_rx2_plugin.dll"
#            "${RX2_AIMP_ROOT}"
#            "${RX2_ARCH}"
#    VERBATIM
#)
]]
