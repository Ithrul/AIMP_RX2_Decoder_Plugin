# AIMP RX2 Decoder Plugin

An input plugin for the Windows version of AIMP that adds playback support for REX / RX2 / RCY loop files using the official REX Shared Library.

## Features
- REX-family input plugin for AIMP with slice-aware playback and correct musical timing.
- Supports RX2 / REX / RCY loop files.
- Slice-aware playback (muted / locked / timed slices).
- Handles BPM, time signature, and bar-length metadata.
- Musical-length-based looping for accurate repetition.
- Reliable seeking and preview behavior.
- Reads available metadata (tempo / structure / creator details).
- Validates headers up front and renders audio in-memory for fast playback and seeking.
- Gracefully handles invalid files.

## Requirements
- **AIMP** (tested with current Windows builds and AIMP 5.40).  
  Download: https://www.aimp.ru/?do=download&os=windows
- **AIMP SDK** (not included).  
  Download from AIMP: https://www.aimp.ru/?do=download&os=windows&cat=sdk  
  Unpack into `external/AIMP_SDK/`.
- **REX SDK (headers)** (not included).  
  Download from Reason Studios: https://www.reasonstudios.com/developer/rex-sdk  
  This project is built against REX SDK **1.9.2** headers.
- **REX Shared Library.dll** (not included in source repository).  
  Any version **1.7 or newer** is compatible for building and runtime use, without
  requiring version-specific workarounds.

  For convenience, the official release packages bundle the following versions:
  - **x64:** REX Shared Library.dll from REX SDK 1.9.2
  - **x86:** REX Shared Library.dll v1.8.1 (last available 32-bit build, provided by Reason Studios after request)

  When building from source, place the DLLs under `external/REX_DLLS/`:
  - `external/REX_DLLS/x64/REX Shared Library.dll`
  - `external/REX_DLLS/x86/REX Shared Library.dll`

- **Visual Studio** with C++ and CMake, or any Windows toolchain capable of building CMake projects.

## Building
The `external/` folder is expected to contain `AIMP_SDK`, `REX_SDK`, and `REX_DLLS`.

1. Ensure the external folders exist:
   - `external/AIMP_SDK/` containing the AIMP SDK headers and libraries.
   - `external/REX_SDK/` containing the REX SDK headers (1.9.2).
   - `external/REX_DLLS/` containing architecture-matching `REX Shared Library.dll` files
     (any version 1.7+).
2. Configure and build with CMake (example):
   ```powershell
   cmake -S . -B build -A x64
   cmake --build build --config Release
   ```
Copy the produced aimp_rx2_plugin.dll and the matching REX Shared Library.dll
(same architecture) into your AIMP plugins folder.

To create the install-ready ZIP package:
```powershell
cmake --build build --config Release --target make_plugin_zip
```
You can also enable automatic packaging by configuring with
`-DAIMP_AUTO_PACKAGE=ON`.

Packaging and auto-deploy helper scripts (disabled by default) are available in
CMakeLists.txt and the tools/ folder. These scripts can generate an install-ready
ZIP package and copy the appropriate REX Shared Library.dll from REX_DLLS based on
architecture. Ensure you comply with the Reason Studios / REX SDK license terms for
any redistribution.

License
This project is released under the MIT License for the original source code only.
See LICENSE for details.

Third-party licenses
This project depends on third-party SDKs that are not included and are licensed separately:

AIMP SDK © AIMP Dev Team

REX SDK / REX Shared Library © Reason Studios AB

The MIT License does not apply to these SDKs or to any binaries provided by them.
Users must obtain and use the REX SDK and REX Shared Library in accordance with the
Reason Studios General License Agreement.
