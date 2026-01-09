cmake_minimum_required(VERSION 3.20)

project(aimp_rx2_plugin VERSION 1.0.0 LANGUAGES C CXX)

# -----------------------------------------------------------------------------
# Settings
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Helps VS Code / IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC niceties
if (MSVC)
  add_compile_options(/permissive- /W4 /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# -----------------------------------------------------------------------------
# Paths (edit these if your repo layout differs)
# -----------------------------------------------------------------------------
set(AIMP_SDK_DIR "${CMAKE_SOURCE_DIR}/external/AIMP_SDK")
set(REX_SDK_DIR  "${CMAKE_SOURCE_DIR}/external/REX_SDK")

# AIMP C++ headers are usually here:
set(AIMP_SDK_CPP_INC "${AIMP_SDK_DIR}/Sources/Cpp")

# REX SDK public headers (where REX.h lives)
set(REX_SDK_INC "${REX_SDK_DIR}")

# -----------------------------------------------------------------------------
# Build info
# -----------------------------------------------------------------------------
if (CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(STATUS "Configuring 32-bit build (x86).")
else()
  message(STATUS "Configuring 64-bit build (x64).")
endif()

# -----------------------------------------------------------------------------
# Target
# -----------------------------------------------------------------------------
add_library(aimp_rx2_plugin SHARED)

target_sources(aimp_rx2_plugin PRIVATE
  src/plugin.cpp
  src/Rx2Decoder.cpp
  src/Rx2DecoderExtension.cpp
  src/Rx2FileFormatExtension.cpp
  src/RexLegacyLoader.cpp
  src/version.rc
)

target_include_directories(aimp_rx2_plugin PRIVATE
  "${AIMP_SDK_CPP_INC}"
  "${REX_SDK_INC}"
)

# Important: we follow Reason's recommendation and DO NOT link against any generated loader .lib
# or compile SDK loader sources. We load the DLL explicitly with LoadLibraryExW in RexLegacyLoader.cpp.

# Defines used across the project (keep in sync with your code)
target_compile_definitions(aimp_rx2_plugin PRIVATE
  aimp_rx2_plugin_EXPORTS
  REX_WINDOWS=1
  REX_DLL_LOADER=1
)

# -----------------------------------------------------------------------------
# Link libraries
# -----------------------------------------------------------------------------
if (WIN32)
  target_link_libraries(aimp_rx2_plugin PRIVATE
    shlwapi
  )
endif()

# -----------------------------------------------------------------------------
# Output name / location
# -----------------------------------------------------------------------------
set_target_properties(aimp_rx2_plugin PROPERTIES
  OUTPUT_NAME "aimp_rx2_plugin"
  PREFIX ""                         # produces aimp_rx2_plugin.dll on Windows
)

# Place binaries in the package folder used by tooling.
set(PACKAGE_ROOT "${CMAKE_SOURCE_DIR}/out/package/aimp_rx2_plugin"
    CACHE PATH "Plugin package root")
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(PACKAGE_BIN_DIR "${PACKAGE_ROOT}/x64")
  set(PACKAGE_ARCH "x64")
  set(PACKAGE_ALLOW_FLAG "-AllowMissingX86")
  set(REX_DLL_SRC "${CMAKE_SOURCE_DIR}/external/REX_DLLS/x64/REX Shared Library.dll")
else()
  set(PACKAGE_BIN_DIR "${PACKAGE_ROOT}")
  set(PACKAGE_ARCH "x86")
  set(PACKAGE_ALLOW_FLAG "-AllowMissingX64")
  set(REX_DLL_SRC "${CMAKE_SOURCE_DIR}/external/REX_DLLS/x86/REX Shared Library.dll")
endif()
set(REX_DLL_DST "${PACKAGE_BIN_DIR}/REX Shared Library.dll")
set(PACKAGE_LIB_DIR "${CMAKE_BINARY_DIR}/import")

set_target_properties(aimp_rx2_plugin PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PACKAGE_BIN_DIR}"
  LIBRARY_OUTPUT_DIRECTORY "${PACKAGE_BIN_DIR}"
  ARCHIVE_OUTPUT_DIRECTORY "${PACKAGE_LIB_DIR}"
)

foreach(config Debug Release RelWithDebInfo MinSizeRel)
  set_target_properties(aimp_rx2_plugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_${config} "${PACKAGE_BIN_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_${config} "${PACKAGE_BIN_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_${config} "${PACKAGE_LIB_DIR}/${config}"
  )
endforeach()

# Copy the correct REX shared library next to the plugin DLL.
if (EXISTS "${REX_DLL_SRC}")
  add_custom_command(TARGET aimp_rx2_plugin POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PACKAGE_BIN_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${REX_DLL_SRC}" "${REX_DLL_DST}"
  )
else()
  message(WARNING "REX Shared Library not found at '${REX_DLL_SRC}'. The plugin will not load without it.")
endif()

# -----------------------------------------------------------------------------
# Tooling helpers
# -----------------------------------------------------------------------------
set(TOOLS_DIR "${CMAKE_SOURCE_DIR}/tools")
set(AIMP_ROOT "" CACHE PATH "Optional AIMP install root for deploy target")
option(AIMP_AUTO_DEPLOY "Deploy plugin to AIMP after build and restart AIMP" OFF)
option(AIMP_AUTO_PACKAGE "Create plugin ZIP after build" ON)

set(DEPLOY_CMD
  "${TOOLS_DIR}/deploy_and_restart_aimp.bat"
  "$<TARGET_FILE:aimp_rx2_plugin>"
  "${AIMP_ROOT}"
  "${PACKAGE_ARCH}"
)

add_custom_target(deploy_aimp
  COMMAND ${DEPLOY_CMD}
  DEPENDS aimp_rx2_plugin
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  USES_TERMINAL
)

if (AIMP_AUTO_DEPLOY)
  add_custom_command(TARGET aimp_rx2_plugin POST_BUILD
    COMMAND ${DEPLOY_CMD}
  )
endif()

if (AIMP_AUTO_PACKAGE)
  add_custom_command(TARGET aimp_rx2_plugin POST_BUILD
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
            "${TOOLS_DIR}/make_plugin_zip.ps1" ${PACKAGE_ALLOW_FLAG}
            -PluginPath "$<TARGET_FILE:aimp_rx2_plugin>" -PluginArch ${PACKAGE_ARCH}
  )
endif()

add_custom_target(make_plugin_zip
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
          "${TOOLS_DIR}/make_plugin_zip.ps1" ${PACKAGE_ALLOW_FLAG}
          -PluginPath "$<TARGET_FILE:aimp_rx2_plugin>" -PluginArch ${PACKAGE_ARCH}
  DEPENDS aimp_rx2_plugin
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  USES_TERMINAL
)
