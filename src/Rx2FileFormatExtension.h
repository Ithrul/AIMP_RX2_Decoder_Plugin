#pragma once

#include "apiFileManager.h"
#include "apiCore.h"

class Rx2FileFormatExtension : public IAIMPExtensionFileFormat
{
public:
    explicit Rx2FileFormatExtension(IAIMPCore* core);
    virtual ~Rx2FileFormatExtension();

    // IUnknown
    HRESULT WINAPI QueryInterface(REFIID riid, void **ppv) override;
    ULONG   WINAPI AddRef() override;
    ULONG   WINAPI Release() override;

    // IAIMPExtensionFileFormat
    HRESULT WINAPI GetDescription(IAIMPString **S) override;
    HRESULT WINAPI GetExtList(IAIMPString **S) override;
    HRESULT WINAPI GetFlags(LongWord *S) override;

private:
    LONG       m_refCount;
    IAIMPCore* m_core;
};
