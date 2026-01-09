#include <windows.h>
#include <string>
#include <algorithm>

#include "RexSdk.h"

// Dynamic loader for "REX Shared Library.dll" shipped locally with the plugin.
// For x86 we ship the last supported Windows 32-bit DLL from Reason SDK 1.8.1.
// The loader only loads from a caller-provided local folder and refuses Windows/System32 paths.


namespace REX {

namespace {

// Exported function names used by the REX DLL.
// Legacy DLLs expose Open/Close; newer DLLs expose REXInitializeDLL/REXUninitializeDLL.

constexpr char kProcOpen[]                = "Open";
constexpr char kProcClose[]               = "Close";
constexpr char kProcREXInitializeDLL[]    = "REXInitializeDLL";
constexpr char kProcREXUninitializeDLL[]  = "REXUninitializeDLL";
constexpr char kProcREXCreate[]           = "REXCreate";
constexpr char kProcREXDelete[]           = "REXDelete";
constexpr char kProcREXGetInfo[]          = "REXGetInfo";
constexpr char kProcREXGetInfoFromBuffer[] = "REXGetInfoFromBuffer";
constexpr char kProcREXGetCreatorInfo[]   = "REXGetCreatorInfo";
constexpr char kProcREXGetSliceInfo[]     = "REXGetSliceInfo";
constexpr char kProcREXSetOutputSampleRate[] = "REXSetOutputSampleRate";
constexpr char kProcREXRenderSlice[]      = "REXRenderSlice";
constexpr char kProcREXStartPreview[]     = "REXStartPreview";
constexpr char kProcREXStopPreview[]      = "REXStopPreview";
constexpr char kProcREXRenderPreviewBatch[] = "REXRenderPreviewBatch";
constexpr char kProcREXSetPreviewTempo[]  = "REXSetPreviewTempo";

HMODULE g_dll = nullptr;

// --- Loader hardening -------------------------------------------------------
// We only load "REX Shared Library.dll" from a caller-provided local folder.
// We do NOT rely on Windows DLL search order (System32/PATH/etc.). We also
// explicitly refuse Windows/System32 locations to prevent accidental pickup.

static std::wstring NormalizePath(const std::wstring& in)
{
    wchar_t full[MAX_PATH] = {0};
    if (GetFullPathNameW(in.c_str(), MAX_PATH, full, nullptr) == 0)
        return in;

    std::wstring out(full);
    std::replace(out.begin(), out.end(), L'/', L'\\');
    while (out.size() > 3 && out.back() == L'\\')
        out.pop_back();
    return out;
}

static bool StartsWithI(const std::wstring& s, const std::wstring& prefix)
{
    if (prefix.empty() || s.size() < prefix.size())
        return false;
    return _wcsnicmp(s.c_str(), prefix.c_str(), prefix.size()) == 0;
}

static bool IsForbiddenSystemLocation(const std::wstring& fullDllPath)
{
    wchar_t sysDir[MAX_PATH] = {0};
    wchar_t winDir[MAX_PATH] = {0};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    GetWindowsDirectoryW(winDir, MAX_PATH);

    std::wstring dll = NormalizePath(fullDllPath);
    std::wstring sys = NormalizePath(sysDir);
    std::wstring win = NormalizePath(winDir);

    if (!sys.empty() && sys.back() != L'\\') sys.push_back(L'\\');
    if (!win.empty() && win.back() != L'\\') win.push_back(L'\\');

    return StartsWithI(dll, sys) || StartsWithI(dll, win);
}

static bool VerifyLoadedFromExpectedPath(HMODULE module, const std::wstring& expectedFullPath)
{
    wchar_t loadedPath[MAX_PATH] = {0};
    if (!module || GetModuleFileNameW(module, loadedPath, MAX_PATH) == 0)
        return false;

    const std::wstring a = NormalizePath(loadedPath);
    const std::wstring b = NormalizePath(expectedFullPath);
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

static bool GetModuleDirectoryFromAddress(const void* addr, std::wstring& outDir)
{
    if (!addr)
        return false;

    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(addr),
            &module))
    {
        return false;
    }

    wchar_t modulePath[MAX_PATH] = {0};
    if (GetModuleFileNameW(module, modulePath, MAX_PATH) == 0)
        return false;

    std::wstring path(modulePath);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return false;

    path.erase(slash);
    outDir = path;
    return true;
}

typedef char (REXCALL *TDLLOpenProc)(void);
typedef void (REXCALL *TDLLCloseProc)(void);
typedef REXError (REXCALL *TREXCreateProc)(REXHandle*, const char [], REX_int32_t,
                                           REXCreateCallback, void*);
typedef void (REXCALL *TREXDeleteProc)(REXHandle*);
typedef REXError (REXCALL *TREXGetInfoProc)(REXHandle, REX_int32_t, REXInfo*);
typedef REXError (REXCALL *TREXGetInfoFromBufferProc)(REX_int32_t, const char [],
                                                      REX_int32_t, REXInfo*);
typedef REXError (REXCALL *TREXGetCreatorInfoProc)(REXHandle, REX_int32_t, REXCreatorInfo*);
typedef REXError (REXCALL *TREXGetSliceInfoProc)(REXHandle, REX_int32_t, REX_int32_t, REXSliceInfo*);
typedef REXError (REXCALL *TREXSetOutputSampleRateProc)(REXHandle, REX_int32_t);
typedef REXError (REXCALL *TREXRenderSliceProc)(REXHandle, REX_int32_t, REX_int32_t, float*[2]);
typedef REXError (REXCALL *TREXStartPreviewProc)(REXHandle);
typedef REXError (REXCALL *TREXStopPreviewProc)(REXHandle);
typedef REXError (REXCALL *TREXRenderPreviewBatchProc)(REXHandle, REX_int32_t, float*[2]);
typedef REXError (REXCALL *TREXSetPreviewTempoProc)(REXHandle, REX_int32_t);

TDLLOpenProc                g_open                = nullptr;
TDLLCloseProc               g_close               = nullptr;
TREXCreateProc              g_rexCreate           = nullptr;
TREXDeleteProc              g_rexDelete           = nullptr;
TREXGetInfoProc             g_rexGetInfo          = nullptr;
TREXGetInfoFromBufferProc   g_rexGetInfoFromBuffer = nullptr;
TREXGetCreatorInfoProc      g_rexGetCreatorInfo   = nullptr;
TREXGetSliceInfoProc        g_rexGetSliceInfo     = nullptr;
TREXSetOutputSampleRateProc g_rexSetSampleRate    = nullptr;
TREXRenderSliceProc         g_rexRenderSlice      = nullptr;
TREXStartPreviewProc        g_rexStartPreview     = nullptr;
TREXStopPreviewProc         g_rexStopPreview      = nullptr;
TREXRenderPreviewBatchProc  g_rexRenderPreview    = nullptr;
TREXSetPreviewTempoProc     g_rexSetPreviewTempo  = nullptr;

bool LoadExports()
{
    g_open = reinterpret_cast<TDLLOpenProc>(GetProcAddress(g_dll, kProcREXInitializeDLL));
    if (!g_open)
    {
        g_open = reinterpret_cast<TDLLOpenProc>(GetProcAddress(g_dll, kProcOpen));
    }

    g_close = reinterpret_cast<TDLLCloseProc>(GetProcAddress(g_dll, kProcREXUninitializeDLL));
    if (!g_close)
    {
        g_close = reinterpret_cast<TDLLCloseProc>(GetProcAddress(g_dll, kProcClose));
    }
    g_rexCreate          = reinterpret_cast<TREXCreateProc>(GetProcAddress(g_dll, kProcREXCreate));
    g_rexDelete          = reinterpret_cast<TREXDeleteProc>(GetProcAddress(g_dll, kProcREXDelete));
    g_rexGetInfo         = reinterpret_cast<TREXGetInfoProc>(GetProcAddress(g_dll, kProcREXGetInfo));
    g_rexGetInfoFromBuffer = reinterpret_cast<TREXGetInfoFromBufferProc>(GetProcAddress(g_dll, kProcREXGetInfoFromBuffer));
    g_rexGetCreatorInfo  = reinterpret_cast<TREXGetCreatorInfoProc>(GetProcAddress(g_dll, kProcREXGetCreatorInfo));
    g_rexGetSliceInfo    = reinterpret_cast<TREXGetSliceInfoProc>(GetProcAddress(g_dll, kProcREXGetSliceInfo));
    g_rexSetSampleRate   = reinterpret_cast<TREXSetOutputSampleRateProc>(GetProcAddress(g_dll, kProcREXSetOutputSampleRate));
    g_rexRenderSlice     = reinterpret_cast<TREXRenderSliceProc>(GetProcAddress(g_dll, kProcREXRenderSlice));
    g_rexStartPreview    = reinterpret_cast<TREXStartPreviewProc>(GetProcAddress(g_dll, kProcREXStartPreview));
    g_rexStopPreview     = reinterpret_cast<TREXStopPreviewProc>(GetProcAddress(g_dll, kProcREXStopPreview));
    g_rexRenderPreview   = reinterpret_cast<TREXRenderPreviewBatchProc>(GetProcAddress(g_dll, kProcREXRenderPreviewBatch));
    g_rexSetPreviewTempo = reinterpret_cast<TREXSetPreviewTempoProc>(GetProcAddress(g_dll, kProcREXSetPreviewTempo));

    return g_open && g_close && g_rexCreate && g_rexDelete && g_rexGetInfo &&
           g_rexGetInfoFromBuffer && g_rexGetCreatorInfo && g_rexGetSliceInfo &&
           g_rexSetSampleRate && g_rexRenderSlice && g_rexStartPreview &&
           g_rexStopPreview && g_rexRenderPreview && g_rexSetPreviewTempo;
}

void ResetState()
{
    g_open = nullptr;
    g_close = nullptr;
    g_rexCreate = nullptr;
    g_rexDelete = nullptr;
    g_rexGetInfo = nullptr;
    g_rexGetInfoFromBuffer = nullptr;
    g_rexGetCreatorInfo = nullptr;
    g_rexGetSliceInfo = nullptr;
    g_rexSetSampleRate = nullptr;
    g_rexRenderSlice = nullptr;
    g_rexStartPreview = nullptr;
    g_rexStopPreview = nullptr;
    g_rexRenderPreview = nullptr;
    g_rexSetPreviewTempo = nullptr;
}

bool IsLoaded()
{
    return g_dll != nullptr && g_open != nullptr;
}

} // namespace

REXError REXInitializeDLL_DirPath(const wchar_t* iDirPath)
{
    if (IsLoaded())
        return kREXImplError_DLLAlreadyLoaded;

    std::wstring pluginDir;
    if (!GetModuleDirectoryFromAddress(reinterpret_cast<const void*>(&GetModuleDirectoryFromAddress), pluginDir))
        return kREXError_DLLNotFound;

    const std::wstring baseDir = NormalizePath(pluginDir);
    if (baseDir.empty())
        return kREXError_DLLNotFound;

    if (iDirPath && iDirPath[0] != L'\0')
    {
        const std::wstring requested = NormalizePath(iDirPath);
        if (_wcsicmp(requested.c_str(), baseDir.c_str()) != 0)
            return kREXError_DLLNotFound;
    }

    std::wstring path(baseDir);
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
        path.append(L"\\");
    path.append(L"REX Shared Library.dll");

    // Always load by absolute path to avoid Windows search order.
    const std::wstring fullPath = NormalizePath(path);

    // Refuse to load from Windows directories (System32/Windows), even if requested.
    if (IsForbiddenSystemLocation(fullPath))
        return kREXError_DLLNotFound;

    // Ensure the file exists at the expected location before loading.
    const DWORD attrs = GetFileAttributesW(fullPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
        return kREXError_DLLNotFound;

    // Load by exact absolute path. Altered search path prioritizes the DLL folder for its dependencies.
    g_dll = LoadLibraryExW(fullPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_dll)
    {
        ResetState();
        return kREXError_DLLNotFound;
    }

    // Verify we loaded exactly the DLL we requested.
    if (!VerifyLoadedFromExpectedPath(g_dll, fullPath))
    {
        FreeLibrary(g_dll);
        g_dll = nullptr;
        ResetState();
        return kREXError_DLLNotFound;
    }

    if (!LoadExports())
    {
        REXUninitializeDLL();
        return kREXError_DLLTooOld;
    }

    // Legacy DLLs expose "Open" instead of REXInitializeDLL; returns non-zero on success.
    char initResult = g_open();
    if (!initResult)
    {
        REXUninitializeDLL();
        return kREXError_DLLTooOld;
    }

    return kREXError_NoError;
}

REXError REXInitializeDLL_PluginDir()
{
    return REXInitializeDLL_DirPath(nullptr);
}

void REXUninitializeDLL(void)
{
    if (g_close)
        g_close();
    ResetState();
    if (g_dll)
    {
        FreeLibrary(g_dll);
        g_dll = nullptr;
    }
}

REXError REXCreate(REXHandle* handle,
    const char buffer[],
    REX_int32_t size,
    REXCreateCallback callbackFunc,
    void* userData)
{
    if (!g_rexCreate)
        return kREXImplError_DLLNotLoaded;
    return g_rexCreate(handle, buffer, size, callbackFunc, userData);
}

void REXDelete(REXHandle* handle)
{
    if (g_rexDelete)
        g_rexDelete(handle);
}

REXError REXGetInfo(REXHandle handle, REX_int32_t rexInfoSize, REXInfo* info)
{
    if (!g_rexGetInfo)
        return kREXImplError_DLLNotLoaded;
    return g_rexGetInfo(handle, rexInfoSize, info);
}

REXError REXGetInfoFromBuffer(REX_int32_t bufferSize,
    const char buffer[],
    REX_int32_t rexInfoSize,
    REXInfo* info)
{
    if (!g_rexGetInfoFromBuffer)
        return kREXImplError_DLLNotLoaded;
    return g_rexGetInfoFromBuffer(bufferSize, buffer, rexInfoSize, info);
}

REXError REXGetCreatorInfo(REXHandle handle,
    REX_int32_t creatorInfoSize,
    REXCreatorInfo* creatorInfo)
{
    if (!g_rexGetCreatorInfo)
        return kREXImplError_DLLNotLoaded;
    return g_rexGetCreatorInfo(handle, creatorInfoSize, creatorInfo);
}

REXError REXGetSliceInfo(REXHandle handle,
    REX_int32_t sliceCount,
    REX_int32_t sliceIndex,
    REXSliceInfo* sliceInfo)
{
    if (!g_rexGetSliceInfo)
        return kREXImplError_DLLNotLoaded;
    return g_rexGetSliceInfo(handle, sliceCount, sliceIndex, sliceInfo);
}

REXError REXSetOutputSampleRate(REXHandle handle, REX_int32_t sampleRate)
{
    if (!g_rexSetSampleRate)
        return kREXImplError_DLLNotLoaded;
    return g_rexSetSampleRate(handle, sampleRate);
}

REXError REXRenderSlice(REXHandle handle,
    REX_int32_t sliceIndex,
    REX_int32_t bufferFrameLength,
    float* outputBuffers[2])
{
    if (!g_rexRenderSlice)
        return kREXImplError_DLLNotLoaded;
    return g_rexRenderSlice(handle, sliceIndex, bufferFrameLength, outputBuffers);
}

REXError REXStartPreview(REXHandle handle)
{
    if (!g_rexStartPreview)
        return kREXImplError_DLLNotLoaded;
    return g_rexStartPreview(handle);
}

REXError REXStopPreview(REXHandle handle)
{
    if (!g_rexStopPreview)
        return kREXImplError_DLLNotLoaded;
    return g_rexStopPreview(handle);
}

REXError REXRenderPreviewBatch(REXHandle handle,
    REX_int32_t framesToRender,
    float* outputBuffers[2])
{
    if (!g_rexRenderPreview)
        return kREXImplError_DLLNotLoaded;
    return g_rexRenderPreview(handle, framesToRender, outputBuffers);
}

REXError REXSetPreviewTempo(REXHandle handle, REX_int32_t tempo)
{
    if (!g_rexSetPreviewTempo)
        return kREXImplError_DLLNotLoaded;
    return g_rexSetPreviewTempo(handle, tempo);
}

} // namespace REX
