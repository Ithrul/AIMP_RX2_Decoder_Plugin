#include "Rx2DecoderExtension.h"
#include "Rx2Decoder.h"
#include "apiObjects.h"
#include "RexSdk.h"
#include <windows.h>
#include <cstring>
#include <wchar.h>
#include <vector>
#include <string>

// Forward declaration so SetErrorInfoFromRexError can call it.
static const wchar_t* GetRexErrorMessageShort(REX::REXError err,
                                              wchar_t* buf,
                                              size_t bufSize);

// Helper to fill IAIMPErrorInfo with a short REX error message.
static void SetErrorInfoFromRexError(IAIMPCore* core,
                                     IAIMPErrorInfo* errorInfo,
                                     REX::REXError err)
{
    if (!errorInfo || !core)
        return;

    IAIMPString* msgStr = nullptr;
    if (SUCCEEDED(core->CreateObject(IID_IAIMPString, (void**)&msgStr)))
    {
        wchar_t buf[256];
        const wchar_t* text = GetRexErrorMessageShort(err, buf, _countof(buf));

        wchar_t tmp[256];
        wcsncpy_s(tmp, text, _TRUNCATE);

        msgStr->SetData(tmp, (int)wcslen(tmp));
        errorInfo->SetInfo(0, msgStr, nullptr);
        msgStr->Release();
    }
}

// Helper to fill IAIMPErrorInfo with a custom text message.
static void SetErrorInfoText(IAIMPCore* core,
                             IAIMPErrorInfo* errorInfo,
                             const std::wstring& msg)
{
    if (!errorInfo || !core || msg.empty())
        return;

    IAIMPString* s = nullptr;
    if (SUCCEEDED(core->CreateObject(IID_IAIMPString, (void**)&s)))
    {
        s->SetData(const_cast<wchar_t*>(msg.c_str()),
                   static_cast<int>(msg.length()));
        errorInfo->SetInfo(0, s, nullptr);
        s->Release();
    }
}

Rx2DecoderExtension::Rx2DecoderExtension(IAIMPCore* core)
    : m_refCount(1)
    , m_core(core)
{
    if (m_core)
        m_core->AddRef();
}

Rx2DecoderExtension::~Rx2DecoderExtension()
{
    if (m_core)
        m_core->Release();
}

// Map REXError to a short message; also allow a formatted fallback.
static const wchar_t* GetRexErrorMessageShort(REX::REXError err,
                                              wchar_t* buf,
                                              size_t bufSize)
{
    switch (err)
    {
    case REX::kREXError_FileCorrupt:
        return L"The format of this file is unknown or the file is corrupt.";

    case REX::kREXError_REX2FileTooNew:
        return L"This REX2 file was created by a later version of "
               L"ReCycle and cannot be loaded with this version of the REX DLL. Please "
               L"update the DLL, then try again.";

    case REX::kREXError_FileHasZeroLoopLength:
        return L"This ReCycle file cannot be used because its 'Bars' and 'Beats' settings have not been set.";

    case kRexError_NoActiveSlices:
        return L"This ReCycle file does not contain any active slices.";

    case REX::kREXError_OutOfMemory:
        return L"Not enough memory to load this REX file.";

#if REX_DLL_LOADER
    case REX::kREXError_NotEnoughMemoryForDLL:
        return L"Not enough memory to load the REX Shared Library.";
    case REX::kREXError_UnableToLoadDLL:
        return L"Unable to load the REX Shared Library (REX Shared Library.dll).";
    case REX::kREXError_DLLTooOld:
        return L"The installed REX Shared Library is too old for this SDK.";
    case REX::kREXError_DLLNotFound:
        return L"REX Shared Library.dll was not found.";
    case REX::kREXError_APITooOld:
        return L"The installed REX Shared Library is newer than this SDK (API too old).";
    case REX::kREXError_OSVersionNotSupported:
        return L"The operating system version is not supported by the REX Shared Library.";
#endif

    case REX::kREXImplError_DLLNotInitialized:
        return L"The REX library is not initialized.";
    case REX::kREXImplError_DLLAlreadyInitialized:
        return L"The REX library is already initialized.";
    case REX::kREXImplError_InvalidSampleRate:
        return L"Invalid sample rate requested from the REX library.";
    case REX::kREXImplError_BufferTooSmall:
        return L"Internal buffer too small in the REX library.";
    case REX::kREXImplError_IsBeingPreviewed:
        return L"REX object is already being previewed.";
    case REX::kREXImplError_NotBeingPreviewed:
        return L"REX object is not being previewed.";
    case REX::kREXImplError_InvalidTempo:
        return L"Invalid tempo requested from the REX library.";
    case REX::kREXImplError_InvalidHandle:
        return L"Invalid handle supplied to the REX library.";
    case REX::kREXImplError_InvalidSize:
        return L"Invalid size specified for a REX info structure.";
    case REX::kREXImplError_InvalidArgument:
        return L"One of the arguments supplied to the REX library is out of range.";
    case REX::kREXImplError_InvalidSlice:
        return L"The requested slice index is out of range for this REX file.";

    case REX::kREXError_OperationAbortedByUser:
        return L"Operation aborted by user.";
    case REX::kREXError_NoCreatorInfoAvailable:
        return L"No creator info is available in this REX file.";

    case REX::kREXError_NoError:
        return L"No error.";

    default:
        break;
    }

    if (buf && bufSize > 0)
    {
        _snwprintf_s(buf, bufSize, _TRUNCATE,
                     L"Unable to open this REX file (REX error code %d).",
                     static_cast<int>(err));
        return buf;
    }

    return L"Unable to open this REX file (unknown REX error).";
}

// IUnknown

HRESULT WINAPI Rx2DecoderExtension::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv)
        return E_POINTER;

    if (riid == IID_IUnknown || riid == IID_IAIMPExtensionAudioDecoder)
    {
        *ppv = static_cast<IAIMPExtensionAudioDecoder*>(this);
        AddRef();
        return S_OK;
    }

    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG WINAPI Rx2DecoderExtension::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

ULONG WINAPI Rx2DecoderExtension::Release()
{
    ULONG r = InterlockedDecrement(&m_refCount);
    if (r == 0)
        delete this;
    return r;
}

// IAIMPExtensionAudioDecoder

// Quick preflight to reject obvious non-REX data (e.g., WAV) before building a
// decoder. Returns true only if the stream looks like a valid REX file.
static bool PreflightStream(IAIMPCore* core,
                            IAIMPStream* stream,
                            IAIMPErrorInfo* errorInfo,
                            REX::REXError& outErr)
{
    outErr = REX::kREXError_NoError;

    if (!stream)
        return false;

    stream->Seek(0, AIMP_STREAM_SEEKMODE_FROM_BEGINNING);
    const INT64 size = stream->GetSize();
    if (size <= 0)
        return false;

    // Fast WAV signature rejection to avoid unnecessary REX work.
    if (size >= 12)
    {
        BYTE hdr[12] = {0};
        int r = stream->Read(hdr, static_cast<int>(sizeof(hdr)));
        stream->Seek(0, AIMP_STREAM_SEEKMODE_FROM_BEGINNING);
        if (r == sizeof(hdr)
            && memcmp(hdr, "RIFF", 4) == 0
            && memcmp(hdr + 8, "WAVE", 4) == 0)
        {
            return false;
        }
    }

    // Read up to 1 MB for fast header validation; fall back to full file on doubt.
    const INT64 kMaxPreflight = 1024 * 1024;
    INT64 toRead = size;
    if (toRead > kMaxPreflight)
        toRead = kMaxPreflight;

    std::vector<BYTE> data;
    try
    {
        data.resize(static_cast<size_t>(toRead));
    }
    catch (...)
    {
        outErr = REX::kREXError_OutOfMemory;
        return false;
    }

    INT64 readBytes = 0;
    while (readBytes < toRead)
    {
        INT64 remaining = toRead - readBytes;
        int chunk = static_cast<int>(remaining > 64 * 1024 ? 64 * 1024 : remaining);
        int r = stream->Read(data.data() + readBytes, chunk);
        if (r <= 0)
            break;
        readBytes += r;
    }

    stream->Seek(0, AIMP_STREAM_SEEKMODE_FROM_BEGINNING);

    if (readBytes <= 0)
        return false;

    REX::REXInfo preInfo{};
    REX::REXError preErr = REX::REXGetInfoFromBuffer(
        static_cast<REX::REX_int32_t>(readBytes),
        reinterpret_cast<const char*>(data.data()),
        static_cast<REX::REX_int32_t>(sizeof(REX::REXInfo)),
        &preInfo);

    // If the truncated read reports FileCorrupt, retry with the full file to avoid false positives.
    if (preErr == REX::kREXError_FileCorrupt && toRead < size)
    {
        try
        {
            data.resize(static_cast<size_t>(size));
        }
        catch (...)
        {
            outErr = REX::kREXError_OutOfMemory;
            return false;
        }

        stream->Seek(0, AIMP_STREAM_SEEKMODE_FROM_BEGINNING);

        readBytes = 0;
        while (readBytes < size)
        {
            INT64 remaining = size - readBytes;
            int chunk = static_cast<int>(remaining > 64 * 1024 ? 64 * 1024 : remaining);
            int r = stream->Read(data.data() + readBytes, chunk);
            if (r <= 0)
                break;
            readBytes += r;
        }

        stream->Seek(0, AIMP_STREAM_SEEKMODE_FROM_BEGINNING);

        if (readBytes <= 0)
            return false;

        preErr = REX::REXGetInfoFromBuffer(
            static_cast<REX::REX_int32_t>(readBytes),
            reinterpret_cast<const char*>(data.data()),
            static_cast<REX::REX_int32_t>(sizeof(REX::REXInfo)),
            &preInfo);
    }

    outErr = preErr;

    if (preErr == REX::kREXError_NoError)
    {
        bool headerOk = true;
        REX::REXError finalErr = preErr;

        if (preInfo.fPPQLength <= 0)
        {
            headerOk = false;
            finalErr = REX::kREXError_FileHasZeroLoopLength;
        }

        // Bars/Beats not set -> both tempo fields zero.
        if (preInfo.fTempo <= 0 && preInfo.fOriginalTempo <= 0)
        {
            headerOk = false;
            finalErr = REX::kREXError_FileHasZeroLoopLength;
        }

        if (preInfo.fChannels <= 0 || preInfo.fChannels > 2)
        {
            headerOk = false;
            finalErr = REX::kREXError_FileCorrupt;
        }

        if (preInfo.fSampleRate <= 0 || preInfo.fSampleRate > 192000)
        {
            headerOk = false;
            finalErr = REX::kREXError_FileCorrupt;
        }

        if (!headerOk)
        {
            outErr = finalErr;
            return false;
        }

        outErr = REX::kREXError_NoError;
        return true;
    }

    return false;
}

HRESULT WINAPI Rx2DecoderExtension::CreateDecoder(IAIMPStream* Stream,
                                                  LongWord /*Flags*/,
                                                  IAIMPErrorInfo* ErrorInfo,
                                                  IAIMPAudioDecoder** Decoder)
{
    if (!Stream || !Decoder)
        return E_POINTER;

    *Decoder = nullptr;

    // Preflight before constructing decoder to block obvious non-REX files.
    REX::REXError preErr = REX::kREXError_NoError;
    if (!PreflightStream(m_core, Stream, ErrorInfo, preErr))
    {
        if (preErr != REX::kREXError_NoError)
        {
            // REX headers are present but invalid.
            SetErrorInfoFromRexError(m_core, ErrorInfo, preErr);
            return E_FAIL;
        }

        return E_FAIL;
    }

    Rx2Decoder* d = new Rx2Decoder(m_core, Stream, false /*skipPreflight*/);

    if (!d->IsValid() || d->HasError())
    {
        if (ErrorInfo && m_core)
        {
            if (d->HasError())
                SetErrorInfoFromRexError(m_core, ErrorInfo, d->GetLastError());
            else
                SetErrorInfoFromRexError(m_core, ErrorInfo, REX::kREXError_FileCorrupt);

            d->Release();
            *Decoder = nullptr;
            return E_FAIL;
        }

        d->Release();
        *Decoder = nullptr;
        return E_FAIL;
    }

    *Decoder = d;
    return S_OK;
}
