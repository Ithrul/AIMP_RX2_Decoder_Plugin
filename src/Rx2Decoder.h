#pragma once

#include "apiDecoders.h"
#include "apiCore.h"
#include "apiFileManager.h"
#include "apiObjects.h"
#include "RexSdk.h"

#include <cstdint>
#include <string>
#include <vector>

// Custom sentinel error codes not provided by the REX SDK.
static constexpr REX::REXError kRexError_NoActiveSlices =
    static_cast<REX::REXError>(10000);

class Rx2Decoder : public IAIMPAudioDecoder
{
public:
    Rx2Decoder(IAIMPCore* core, IAIMPStream* stream, bool skipPreflight = false);
    virtual ~Rx2Decoder();

    // IUnknown
    HRESULT WINAPI QueryInterface(REFIID riid, void **ppv) override;
    ULONG   WINAPI AddRef() override;
    ULONG   WINAPI Release() override;

    // IAIMPAudioDecoder
    BOOL    WINAPI GetFileInfo(IAIMPFileInfo *FileInfo) override;
    BOOL    WINAPI GetStreamInfo(int *SampleRate, int *Channels, int *SampleFormat) override;
    BOOL    WINAPI IsSeekable() override;
    BOOL    WINAPI IsRealTimeStream() override;
    INT64   WINAPI GetAvailableData() override;
    INT64   WINAPI GetSize() override;
    INT64   WINAPI GetPosition() override;
    BOOL    WINAPI SetPosition(const INT64 Value) override;
    int     WINAPI Read(void *Buffer, int Count) override;

    // Helpers for extension
    bool          IsValid()     const { return m_isValid; }
    bool          HasError()    const { return m_hasError; }
    REX::REXError GetLastError() const { return m_lastError; }

private:
    LONG         m_refCount;
    IAIMPCore   *m_core;
    IAIMPStream *m_stream;

    std::uint8_t *m_fileData;
    std::int64_t  m_fileSize;

    int    m_sampleRate;
    int    m_channels;
    int    m_sourceSampleRate; // original rate from REX (info.fSampleRate)

    std::int64_t  m_totalSamples;      // frames
    std::int64_t  m_positionSamples;   // frames
    std::int64_t  m_loopFrames;        // frames
    std::int64_t  m_bytesServed;       // bytes returned via Read()

    REX::REX_int32_t m_previewTempo;      // 1/1000 BPM
    bool             m_hasTempoFromFile;

    // Creator metadata (from REXCreatorInfo). Empty strings mean "not available".
    std::wstring m_creatorName;
    std::wstring m_creatorCopyright;
    std::wstring m_creatorURL;
    std::wstring m_creatorEmail;
    std::wstring m_creatorFreeText;

    REX::REXHandle   m_rexHandle;
    bool             m_isValid;
    bool             m_skipPreflight;

    REX::REXError    m_lastError;
    bool             m_hasError;

    std::vector<float> m_pcmData;
};
