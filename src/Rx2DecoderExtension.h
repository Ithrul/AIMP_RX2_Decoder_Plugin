#pragma once

#include "apiDecoders.h"
#include "apiCore.h"

class Rx2DecoderExtension : public IAIMPExtensionAudioDecoder
{
public:
    explicit Rx2DecoderExtension(IAIMPCore* core);
    virtual ~Rx2DecoderExtension();

    // IUnknown
    HRESULT WINAPI QueryInterface(REFIID riid, void** ppv) override;
    ULONG   WINAPI AddRef() override;
    ULONG   WINAPI Release() override;

    // IAIMPExtensionAudioDecoder
    HRESULT WINAPI CreateDecoder(IAIMPStream* Stream,
                                 LongWord Flags,
                                 IAIMPErrorInfo* ErrorInfo,
                                 IAIMPAudioDecoder** Decoder) override;

private:
    LONG       m_refCount;
    IAIMPCore* m_core;
};
