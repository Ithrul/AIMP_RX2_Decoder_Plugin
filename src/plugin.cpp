#include <windows.h>
#include <shlwapi.h>

#include "apiPlugin.h"
#include "apiCore.h"
#include "apiFileManager.h"

#include "RexSdk.h"
#include "Rx2DecoderExtension.h"
#include "Rx2FileFormatExtension.h"

#pragma comment(lib, "Shlwapi.lib")

#ifndef AIMP_PLUGIN_INFO_VERSION
#define AIMP_PLUGIN_INFO_VERSION 0x4
#endif

// Forward declaration so we can use its address in GetModuleHandleExW
extern "C" HRESULT __declspec(dllexport) WINAPI AIMPPluginGetHeader(void **plugin);

class Rx2Plugin : public IAIMPPlugin
{
public:
    Rx2Plugin();
    virtual ~Rx2Plugin();

    // IUnknown
    HRESULT WINAPI QueryInterface(REFIID riid, void **ppv) override;
    ULONG   WINAPI AddRef() override;
    ULONG   WINAPI Release() override;

    // IAIMPPlugin
    TChar*  WINAPI InfoGet(int index) override;
    DWORD   WINAPI InfoGetCategories() override;
    HRESULT WINAPI Initialize(IAIMPCore *core) override;
    HRESULT WINAPI Finalize() override;
    void    WINAPI SystemNotification(int NotifyID, IUnknown *Data) override;

private:
    LONG                    m_refCount;
    IAIMPCore              *m_core;
    Rx2DecoderExtension    *m_decoderExt;
    Rx2FileFormatExtension *m_fileFormatExt;
    bool                    m_rexInitialized;
};

Rx2Plugin::Rx2Plugin()
    : m_refCount(1)
    , m_core(nullptr)
    , m_decoderExt(nullptr)
    , m_fileFormatExt(nullptr)
    , m_rexInitialized(false)
{
}

Rx2Plugin::~Rx2Plugin()
{
}

// IUnknown

HRESULT WINAPI Rx2Plugin::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;

    if (riid == IID_IUnknown)
    {
        *ppv = static_cast<IAIMPPlugin*>(this);
        AddRef();
        return S_OK;
    }

    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG WINAPI Rx2Plugin::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

ULONG WINAPI Rx2Plugin::Release()
{
    ULONG r = InterlockedDecrement(&m_refCount);
    if (r == 0)
        delete this;
    return r;
}

// IAIMPPlugin

TChar* WINAPI Rx2Plugin::InfoGet(int index)
{
    // 0 = Name, 1 = Author, 2 = Short description, 3 = Full description, 4 = Version
    static TChar name[]      = L"RX2 Decoder";
    static TChar author[]    = L"Ivan Smirnoff";
    static TChar shortDesc[] = L"Adds support for REX / RX2 loop files.";
    static TChar fullDesc[]  = L"REX-family input plugin for AIMP with slice-aware playback and correct musical timing.\n\nCapabilities:\n- RX2 / REX / RCY support\n- Slice-aware playback (muted/locked/timed slices)\n- BPM, time signature and bar-length handling\n- Musical-length-based looping\n- Reliable seeking and preview\n- Reads available metadata (tempo/structure/creator)\n- Proper handling of invalid files";
    static TChar version[]   = L"0.9.6";
    

    switch (index)
    {
    case 0:  return name;
    case 1:  return author;
    case 2:  return shortDesc;
    case 3:  return fullDesc;
    case AIMP_PLUGIN_INFO_VERSION: return version;
    default: return nullptr;
    }
}

DWORD WINAPI Rx2Plugin::InfoGetCategories()
{
    return AIMP_PLUGIN_CATEGORY_DECODERS;
}

HRESULT WINAPI Rx2Plugin::Initialize(IAIMPCore *core)
{
    m_core = core;
    if (m_core)
        m_core->AddRef();

    // --- 1) Initialize REX DLL (plugin folder first, then AIMP.exe folder) ---

    if (!m_rexInitialized)
    {
        wchar_t dir[MAX_PATH] = {0};

        // Plugin DLL folder
        HMODULE hModule = nullptr;
        if (GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&AIMPPluginGetHeader),
                &hModule))
        {
            if (GetModuleFileNameW(hModule, dir, MAX_PATH) > 0)
            {
                PathRemoveFileSpecW(dir);

                REX::REXError err = REX::REXInitializeDLL_DirPath(dir);
                if (err == REX::kREXError_NoError)
                {
                    m_rexInitialized = true;
                }
            }
        }

        // AIMP.exe folder (fallback)
        if (!m_rexInitialized)
        {
            if (GetModuleFileNameW(nullptr, dir, MAX_PATH) > 0)
            {
                PathRemoveFileSpecW(dir);

                REX::REXError err = REX::REXInitializeDLL_DirPath(dir);
                if (err == REX::kREXError_NoError)
                {
                    m_rexInitialized = true;
                }
            }
        }
    }

    if (!m_rexInitialized)
    {
        if (m_core)
        {
            m_core->Release();
            m_core = nullptr;
        }
        return E_FAIL;
    }

    // --- 2) Register decoder and file format extensions ---

    m_decoderExt    = new Rx2DecoderExtension(m_core);
    m_fileFormatExt = new Rx2FileFormatExtension(m_core);

    if (m_core)
    {
        m_core->RegisterExtension(IID_IAIMPServiceAudioDecoders, m_decoderExt);
        m_core->RegisterExtension(IID_IAIMPServiceFileFormats, m_fileFormatExt);
    }

    return S_OK;
}

HRESULT WINAPI Rx2Plugin::Finalize()
{
    if (m_core)
    {
        if (m_decoderExt)
        {
            m_core->UnregisterExtension(m_decoderExt);
            m_decoderExt->Release();
            m_decoderExt = nullptr;
        }

        if (m_fileFormatExt)
        {
            m_core->UnregisterExtension(m_fileFormatExt);
            m_fileFormatExt->Release();
            m_fileFormatExt = nullptr;
        }
    }

    if (m_rexInitialized)
    {
        REX::REXUninitializeDLL();
        m_rexInitialized = false;
    }

    if (m_core)
    {
        m_core->Release();
        m_core = nullptr;
    }

    return S_OK;
}

void WINAPI Rx2Plugin::SystemNotification(int /*NotifyID*/, IUnknown* /*Data*/)
{
    // No notifications handled for now.
}

// Export

extern "C" HRESULT __declspec(dllexport) WINAPI AIMPPluginGetHeader(void **plugin)
{
    if (!plugin)
    {
        return E_POINTER;
    }

    *plugin = new Rx2Plugin();
    return S_OK;
}
