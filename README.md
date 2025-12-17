# AIMP RX2 Decoder Plugin

An input plugin for the Windows version of AIMP that adds playback support for REX / RX2 / RCY loop files using the official REX Shared Library.

## Features
- REX-family input plugin for AIMP with slice-aware playback and correct musical timing.
- Supports RX2 / REX / RCY files.
- Slice-aware playback (muted / locked / timed slices).
- Handles BPM, time signature, and bar-length metadata.
- Musical-length-based looping for accurate repetition.
- Reliable seeking and preview behavior.
- Reads available metadata (tempo / structure / creator details).
- Validates headers up front and renders audio in-memory for fast playback and seeking.
- Gracefully handles invalid files.

## Requirements
- **AIMP** (tested with current Windows builds and AIMP 5.40). Download: https://www.aimp.ru/?do=download&os=windows
- **AIMP SDK** (not included). Download from AIMP: https://www.aimp.ru/?do=download&os=windows&cat=sdk and unpack into `external/AIMP_SDK/`. 
- **REX SDK + REX Shared Library** (not included). Download from Reason Studios: https://www.reasonstudios.com/developer/rex-sdk  
  Place the SDK contents (including the `REX Shared Library.dll`) under `external/REX_SDK/`, keeping the directory layout expected by the SDK. x64 requires REX 1.7+ (e.g., 1.9.x). For 32-bit builds you must supply a legally obtained x86 REX Shared Library compatible with your license.
- **Visual Studio** with C++ and CMake, or any Windows toolchain that can build CMake projects.

## Building
1) Ensure the SDK folders exist:
   - `external/AIMP_SDK/` containing the AIMP SDK headers/libs.
   - `external/REX_SDK/` containing the REX SDK (including `Win/x64/Deployment/REX Shared Library.dll`).
2) Configure with CMake (example):
   ```powershell
   cmake -S . -B build -A x64
   cmake --build build --config Release
   ```
3) Copy the produced `aimp_rx2_plugin.dll` (and a matching `REX Shared Library.dll` if not already provided by the user's system) into your AIMP plugins folder.

4) There is commented out packaging scripts in CMakeLists.txt and 'tools/' folder which creates install ready zip of plugin and copies REX Shared Library.dll from your local SDK installation; ensure you comply with the Reason/REX SDK license terms for any redistribution.

## License
This project is released under the MIT License **for the original source code only**. See `LICENSE` for details.

## Third-party licenses

This project depends on third-party SDKs that are **not included** and are licensed separately:
- AIMP SDK — © AIMP Dev Team
- REX SDK / REX Shared Library — © Reason Studios AB (The Reason REX SDK license text is included in this repository for reference).

The MIT License does **not** apply to these SDKs or to any binaries provided by them.
Users must obtain and use the REX SDK in accordance with the Reason Studios
General License Agreement.
