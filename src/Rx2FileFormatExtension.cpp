#include "Rx2FileFormatExtension.h"
#include "apiObjects.h"

Rx2FileFormatExtension::Rx2FileFormatExtension(IAIMPCore* core)
    : m_refCount(1)
    , m_core(core)
{
    if (m_core)
        m_core->AddRef();
}

Rx2FileFormatExtension::~Rx2FileFormatExtension()
{
    if (m_core)
        m_core->Release();
}

// IUnknown

HRESULT WINAPI Rx2FileFormatExtension::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;

    if (riid == IID_IUnknown || riid == IID_IAIMPExtensionFileFormat)
    {
        *ppv = static_cast<IAIMPExtensionFileFormat*>(this);
        AddRef();
        return S_OK;
    }

    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG WINAPI Rx2FileFormatExtension::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

ULONG WINAPI Rx2FileFormatExtension::Release()
{
    ULONG r = InterlockedDecrement(&m_refCount);
    if (r == 0)
        delete this;
    return r;
}

// IAIMPExtensionFileFormat

HRESULT WINAPI Rx2FileFormatExtension::GetDescription(IAIMPString **S)
{
    if (!S)
        return E_POINTER;
    *S = nullptr;

    if (!m_core)
        return E_FAIL;

    IAIMPString* str = nullptr;
    if (FAILED(m_core->CreateObject(IID_IAIMPString, (void**)&str)))
        return E_FAIL;

    const wchar_t* text = L"REX / RX2 loop files";
    str->SetData(const_cast<wchar_t*>(text), (int)wcslen(text));

    *S = str;
    return S_OK;
}

HRESULT WINAPI Rx2FileFormatExtension::GetExtList(IAIMPString **S)
{
    if (!S)
        return E_POINTER;
    *S = nullptr;

    if (!m_core)
        return E_FAIL;

    IAIMPString* str = nullptr;
    if (FAILED(m_core->CreateObject(IID_IAIMPString, (void**)&str)))
        return E_FAIL;

    const wchar_t* extList = L"*.rx2;*.rex;*.rcy";
    str->SetData(const_cast<wchar_t*>(extList), (int)wcslen(extList));

    *S = str;
    return S_OK;
}

HRESULT WINAPI Rx2FileFormatExtension::GetFlags(LongWord *S)
{
    if (!S)
        return E_POINTER;

    *S = AIMP_SERVICE_FILEFORMATS_CATEGORY_AUDIO;
    return S_OK;
}
