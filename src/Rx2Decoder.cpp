#include "Rx2Decoder.h"

#include <cstdint>
#include <cstddef>
#include <string>
#include <cstring>
#include <windows.h>

// ---------------- helpers ----------------

static float ClampSampleFloat(float v)
{
    if (v < -1.0f) return -1.0f;
    if (v >  1.0f) return  1.0f;
    return v;
}

static INT64 BytesToFrames(INT64 bytes, int channels, int bytesPerSample)
{
    if (channels <= 0 || bytesPerSample <= 0)
        return 0;

    const INT64 frameSize = static_cast<INT64>(channels) * bytesPerSample;
    if (frameSize <= 0)
        return 0;

    return bytes / frameSize;
}

static INT64 FramesToBytes(INT64 frames, int channels, int bytesPerSample)
{
    if (frames <= 0 || channels <= 0 || bytesPerSample <= 0)
        return 0;

    const INT64 frameSize = static_cast<INT64>(channels) * bytesPerSample;
    return frames * frameSize;
}

static REX::REXCallbackResult RexProgressCallback(REX::REX_int32_t /*percentFinished*/, void* /*ud*/)
{
    return REX::kREXCallback_Continue;
}

// Convert a narrow C string from the REX SDK (UTF-8 safe ASCII) into std::wstring.
static std::wstring RexStringToWide(const char* s)
{
    if (!s || !*s)
        return std::wstring();

    int len = static_cast<int>(strlen(s));
    if (len <= 0)
        return std::wstring();

    int needed = MultiByteToWideChar(CP_UTF8, 0, s, len, nullptr, 0);
    if (needed <= 0)
        return std::wstring();

    std::wstring out;
    out.resize(static_cast<size_t>(needed));
    MultiByteToWideChar(CP_UTF8, 0, s, len, &out[0], needed);
    return out;
}

// ---------------- REXCreate sandbox ----------------

// Context for running REXCreate in a worker thread
struct REXCreateContext
{
    const char*      data;
    REX::REX_int32_t size;
    REX::REXHandle   handle;
    REX::REXError    err;
    HANDLE           doneEvent;
};

// Worker thread: calls REXCreate and signals when done
static DWORD WINAPI REXCreateThreadProc(LPVOID param)
{
    REXCreateContext* ctx = reinterpret_cast<REXCreateContext*>(param);

    ctx->err = REX::REXCreate(
        &ctx->handle,
        ctx->data,
        ctx->size,
        RexProgressCallback,
        nullptr);

    SetEvent(ctx->doneEvent);
    return 0;
}

// ---------------- ctor / dtor ----------------

Rx2Decoder::Rx2Decoder(IAIMPCore* core, IAIMPStream* stream, bool skipPreflight)
    : m_refCount(1)
    , m_core(core)
    , m_stream(stream)
    , m_fileData(nullptr)
    , m_fileSize(0)
    , m_sampleRate(44100)
    , m_sourceSampleRate(0)
    , m_channels(2)
    , m_totalSamples(0)
    , m_positionSamples(0)
    , m_loopFrames(0)
    , m_bytesServed(0)
    , m_previewTempo(0)
    , m_hasTempoFromFile(false)
    , m_creatorName()
    , m_creatorCopyright()
    , m_creatorURL()
    , m_creatorEmail()
    , m_creatorFreeText()
    , m_rexHandle(nullptr)
    , m_isValid(false)
    , m_skipPreflight(skipPreflight)
    , m_lastError(REX::kREXError_NoError)
    , m_hasError(false)
{
    if (m_core)
        m_core->AddRef();
    if (m_stream)
        m_stream->AddRef();

    if (!m_stream)
        return;

    // 1) read whole file
    m_stream->Seek(0, AIMP_STREAM_SEEKMODE_FROM_BEGINNING);
    const INT64 totalSize = m_stream->GetSize();
    if (totalSize <= 0)
        return;

    m_fileData = new std::uint8_t[static_cast<size_t>(totalSize)];
    m_fileSize = 0;

    while (m_fileSize < totalSize)
    {
        INT64 remaining = totalSize - m_fileSize;
        int toRead = static_cast<int>(remaining > 64 * 1024 ? 64 * 1024 : remaining);
        int r = m_stream->Read(m_fileData + m_fileSize, toRead);
        if (r <= 0)
            break;
        m_fileSize += r;
    }

    if (m_fileSize <= 0)
        return;

    bool headersOk = m_skipPreflight; // assume preflight done by caller if skipped

    
    // 1.5) Preflight: skip here if already performed by caller/extension
    if (!m_skipPreflight)
    {
       
        REX::REXInfo preInfo{};
        REX::REXError preErr = REX::REXGetInfoFromBuffer(
            static_cast<REX::REX_int32_t>(m_fileSize),
            reinterpret_cast<const char*>(m_fileData),
            static_cast<REX::REX_int32_t>(sizeof(REX::REXInfo)),
            &preInfo);

        if (preErr != REX::kREXError_NoError)
        {
            m_lastError = preErr;
            m_hasError  = true;
            m_isValid   = false;
            m_totalSamples = 0;
            m_positionSamples = 0;
            m_loopFrames = 0;
            m_previewTempo = 0;
            m_fileSize = 0;
            m_pcmData.clear();

            if (m_stream)
            {
                m_stream->Release();
                m_stream = nullptr;
            }
            delete[] m_fileData;
            m_fileData = nullptr;
            return;
        }
    
        // Additional cheap sanity checks on header fields
        bool headerOk = true;
      
        // Explicitly detect missing loop length to show a specific error.
        if (preInfo.fPPQLength <= 0)
        {
            m_lastError = REX::kREXError_FileHasZeroLoopLength;
            m_hasError  = true;
            m_isValid   = false;
            headerOk = false;
        }

        // Channels must be 1 or 2 for our decoder
        if (preInfo.fChannels <= 0 || preInfo.fChannels > 2)
            headerOk = false;

        // Sample rate must be sane
        if (preInfo.fSampleRate <= 0 || preInfo.fSampleRate > 192000)
            headerOk = false;

        // PPQ length must be positive and not absurdly large
        if (preInfo.fPPQLength <= 0 || preInfo.fPPQLength > 100000000)
            headerOk = false;

        // Bars/Beats not set -> both tempo fields zero.
        if (preInfo.fTempo <= 0 && preInfo.fOriginalTempo <= 0)
            headerOk = false;

        // Time signature denominator should be common musical values
        if (!(preInfo.fTimeSignDenom == 1 ||
              preInfo.fTimeSignDenom == 2 ||
              preInfo.fTimeSignDenom == 4 ||
              preInfo.fTimeSignDenom == 8 ||
              preInfo.fTimeSignDenom == 16))
        {
            headerOk = false;
        }

        if (!headerOk)
        {
            m_lastError = REX::kREXError_FileCorrupt;
            m_hasError  = true;
            m_isValid   = false;
            return;
        }

        headersOk = true;
    }

    if (!headersOk)
    {
        m_lastError = REX::kREXError_FileCorrupt;
        m_hasError  = true;
        m_isValid   = false;
        return;
    }

    
    // 2) create REX handle, sandboxed in a worker thread with timeout
    REX::REXError err = REX::kREXError_NoError;

    
    REXCreateContext ctx{};
    ctx.data      = reinterpret_cast<const char*>(m_fileData);
    ctx.size      = static_cast<REX::REX_int32_t>(m_fileSize);
    ctx.handle    = nullptr;
    ctx.err       = REX::kREXError_NoError;
    ctx.doneEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    if (!ctx.doneEvent)
    {
        m_lastError = REX::kREXError_OutOfMemory;
        m_hasError  = true;
        m_isValid   = false;
        return;
    }

    HANDLE thread = CreateThread(
        nullptr,
        0,
        REXCreateThreadProc,
        &ctx,
        0,
        nullptr);

    if (!thread)
    {
        CloseHandle(ctx.doneEvent);
        m_lastError = REX::kREXError_OutOfMemory;
        m_hasError  = true;
        m_isValid   = false;
        return;
    }

    // Wait for completion or timeout
    const DWORD TIMEOUT_MS = 2000; // 2 seconds
    DWORD waitRes = WaitForSingleObject(ctx.doneEvent, TIMEOUT_MS);

    if (waitRes == WAIT_OBJECT_0)
    {
        // Completed normally
        CloseHandle(ctx.doneEvent);
        CloseHandle(thread);

        err         = ctx.err;
        m_rexHandle = ctx.handle;
    }
    else
    {
        // WAIT_TIMEOUT or WAIT_FAILED: REXCreate hung inside the DLL
        TerminateThread(thread, 1);
        CloseHandle(thread);
        CloseHandle(ctx.doneEvent);

        m_lastError = REX::kREXError_FileCorrupt;
        m_hasError  = true;
        m_isValid   = false;
        return;
    }
    

    // Hard failure if handle is null
    if (!m_rexHandle)
    {
        m_lastError = (err != REX::kREXError_NoError)
                        ? err
                        : REX::kREXError_Undefined;
        m_hasError  = true;
        m_isValid   = false;
        return;
    }
    

    // If err is non-zero but handle is valid, treat as non-fatal warning.
    if (err != REX::kREXError_NoError)
    {
        // Loop length not set must be fatal.
        if (err == REX::kREXError_FileHasZeroLoopLength)
        {
            m_lastError = err;
            m_hasError  = true;
            m_isValid   = false;

            if (m_rexHandle)
            {
                REX::REXDelete(&m_rexHandle);
                m_rexHandle = nullptr;
            }
            return;
        }

        m_lastError = err;
        m_hasError  = true;
    }
        

    // 3) get info
    REX::REXInfo info{};
    err = REX::REXGetInfo(
        m_rexHandle,
        static_cast<REX::REX_int32_t>(sizeof(REX::REXInfo)),
        &info);

    // Detect missing loop length to surface the correct error code.
    if (info.fPPQLength <= 0)
    {
        m_lastError = REX::kREXError_FileHasZeroLoopLength;
        m_hasError  = true;
        m_isValid   = false;
        return;
    }

    m_channels         = info.fChannels;
    m_sourceSampleRate = info.fSampleRate;

    // 3.5) optional creator metadata
    {
        REX::REXCreatorInfo creator{};
        REX::REXError cErr = REX::REXGetCreatorInfo(
            m_rexHandle,
            static_cast<REX::REX_int32_t>(sizeof(REX::REXCreatorInfo)),
            &creator);

        if (cErr == REX::kREXError_NoError)
        {
            m_creatorName      = RexStringToWide(creator.fName);
            m_creatorCopyright = RexStringToWide(creator.fCopyright);
            m_creatorURL       = RexStringToWide(creator.fURL);
            m_creatorEmail     = RexStringToWide(creator.fEmail);
            m_creatorFreeText  = RexStringToWide(creator.fFreeText);
        }
    }

    // Use original file sample rate for playback; fallback to 44100 if missing
    if (m_sourceSampleRate > 0)
        m_sampleRate = m_sourceSampleRate;
    else
        m_sampleRate = 44100;

    // Missing both tempo fields implies Bars/Beats not set.
    if (info.fTempo <= 0 && info.fOriginalTempo <= 0)
    {
        m_lastError = REX::kREXError_FileHasZeroLoopLength;
        m_hasError  = true;
        m_isValid   = false;
        return;
    }

    // choose tempo: prefer exported tempo, then original tempo
    REX::REX_int32_t tempo = 0;
    bool hasTempo = false;

    if (info.fTempo > 0)
    {
        tempo    = info.fTempo;
        hasTempo = true;
    }
    else if (info.fOriginalTempo > 0)
    {
        tempo    = info.fOriginalTempo;
        hasTempo = true;
    }
    else
    {
        tempo    = 120000; // 120.000 BPM
        hasTempo = false;
    }
        
    m_previewTempo     = tempo;
    m_hasTempoFromFile = hasTempo;

    // 4) set sample rate & compute length (same as PreviewRenderInTempo)
    REX::REXSetOutputSampleRate(
        m_rexHandle,
        static_cast<REX::REX_int32_t>(m_sampleRate));

    double tmp = static_cast<double>(m_sampleRate);
    tmp *= 1000.0;
    tmp *= static_cast<double>(info.fPPQLength);

    // Convert tempo to quarter-note BPM depending on time signature denominator.
    double tempoForLength = static_cast<double>(m_previewTempo);
    int    denom          = info.fTimeSignDenom;

    if (denom > 0 && denom != 4)
    {
        tempoForLength = tempoForLength * 4.0 / static_cast<double>(denom);
    }

    tmp /= (tempoForLength * 256.0);

    if (tmp < 0.0)
        tmp = 0.0;
      
    REX::REX_int32_t lengthFrames = static_cast<REX::REX_int32_t>(tmp);

    if (lengthFrames <= 0 || m_sampleRate <= 0 || m_channels <= 0)
    {
        m_lastError = REX::kREXError_FileCorrupt;
        m_hasError  = true;
        m_isValid   = false;
        return;
    }
    

    // initial guess; we'll overwrite with actual framesRendered later
    m_loopFrames      = static_cast<INT64>(lengthFrames);
    m_totalSamples    = m_loopFrames;
    m_positionSamples = 0;

    // sanity bound ~1 hour
    {
        double durationSec = static_cast<double>(m_loopFrames)
                           / static_cast<double>(m_sampleRate);
        const double kMaxDurationSec = 60.0 * 60.0;

        if (durationSec <= 0.0 || durationSec > kMaxDurationSec)
        {
            m_lastError = REX::kREXError_FileCorrupt;
            m_hasError  = true;
            m_isValid   = false;
            return;
        }
    }

    // 5) render preview into channel-separated buffers
    std::vector<float> renderSamples;
    try
    {
        renderSamples.resize(static_cast<size_t>(info.fChannels)
                             * static_cast<size_t>(lengthFrames));
    }
    catch (...)
    {
        m_lastError = REX::kREXError_OutOfMemory;
        m_hasError  = true;
        m_isValid   = false;
        return;
    }

    float* buffers[2] = { nullptr, nullptr };
    buffers[0] = renderSamples.data();
    if (info.fChannels == 2)
        buffers[1] = renderSamples.data() + lengthFrames;
    else
        buffers[1] = nullptr;

    // Set tempo (already using m_previewTempo from above)
    err = REX::REXSetPreviewTempo(m_rexHandle, m_previewTempo);
    if (err != REX::kREXError_NoError)
    {
        m_lastError = err;
        m_hasError  = true;
        m_isValid   = false;
        return;
    }

    err = REX::REXStartPreview(m_rexHandle);
    if (err != REX::kREXError_NoError)
    {
        m_lastError = err;
        m_hasError  = true;
        m_isValid   = false;
        return;
    }

    REX::REX_int32_t framesRendered = 0;

    while (framesRendered != lengthFrames)
    {
        REX::REX_int32_t remaining = lengthFrames - framesRendered;
        REX::REX_int32_t todo      = remaining > 64 ? 64 : remaining;

        float* tmpBuf[2] = { nullptr, nullptr };
        tmpBuf[0] = buffers[0] + framesRendered;
        if (buffers[1])
            tmpBuf[1] = buffers[1] + framesRendered;

        err = REX::REXRenderPreviewBatch(m_rexHandle, todo, tmpBuf);
        if (err != REX::kREXError_NoError)
        {
            m_lastError = err;
            m_hasError  = true;
            m_isValid   = false;
            break;
        }

        framesRendered += todo;
    }

    REX::REXStopPreview(m_rexHandle);

    // SDK does a small extra batch after StopPreview.
    {
        float left[64];
        float right[64];
        float* tmpRenderBuffers[2];

        tmpRenderBuffers[0] = &left[0];
        tmpRenderBuffers[1] = (m_channels > 1) ? &right[0] : nullptr;

        (void)REX::REXRenderPreviewBatch(m_rexHandle, 64, tmpRenderBuffers);
    }

    // 6) copy to interleaved m_pcmData
    const int   ch   = m_channels;
    const INT64 nFrm = framesRendered;

    try
    {
        m_pcmData.resize(static_cast<size_t>(nFrm) * ch);
    }
    catch (...)
    {
        m_lastError = REX::kREXError_OutOfMemory;
        m_hasError  = true;
        m_isValid   = false;
        return;
    }

    const float* left  = buffers[0];
    const float* right = buffers[1];

    for (INT64 f = 0; f < nFrm; ++f)
    {
        float l = left[f];
        float r = (ch > 1 && right) ? right[f] : l;

        size_t base = static_cast<size_t>(f) * ch;
        m_pcmData[base + 0] = ClampSampleFloat(l);
        if (ch > 1)
            m_pcmData[base + 1] = ClampSampleFloat(r);
    }

    m_totalSamples = nFrm;
    m_loopFrames   = nFrm;

    // Detect files that render as complete silence (e.g., all slices muted).
    {
        const float kSilenceThreshold = 1e-7f;
        bool hasActiveSamples = false;

        for (float sample : m_pcmData)
        {
            if (sample < -kSilenceThreshold || sample > kSilenceThreshold)
            {
                hasActiveSamples = true;
                break;
            }
        }

        if (!hasActiveSamples || nFrm <= 0)
        {
            m_lastError = kRexError_NoActiveSlices;
            m_hasError  = true;
            m_isValid   = false;
            return;
        }
    }

    if (m_rexHandle)
    {
        REX::REXDelete(&m_rexHandle);
        m_rexHandle = nullptr;
    }

    m_isValid = true;
}


Rx2Decoder::~Rx2Decoder()
{
    if (m_rexHandle)
    {
        REX::REXDelete(&m_rexHandle);
        m_rexHandle = nullptr;
    }

    delete[] m_fileData;
    m_fileData = nullptr;
    m_fileSize = 0;

    if (m_stream)
    {
        m_stream->Release();
        m_stream = nullptr;
    }

    if (m_core)
    {
        m_core->Release();
        m_core = nullptr;
    }
}

// ---------------- IUnknown ----------------

HRESULT WINAPI Rx2Decoder::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;

    if (riid == IID_IUnknown || riid == IID_IAIMPAudioDecoder)
    {
        *ppv = static_cast<IAIMPAudioDecoder*>(this);
        AddRef();
        return S_OK;
    }

    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG WINAPI Rx2Decoder::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

ULONG WINAPI Rx2Decoder::Release()
{
    ULONG r = InterlockedDecrement(&m_refCount);
    if (r == 0)
        delete this;
    return r;
}

// ---------------- IAIMPAudioDecoder ----------------

BOOL WINAPI Rx2Decoder::GetFileInfo(IAIMPFileInfo *FileInfo)
{
    if (!m_isValid || !FileInfo)
        return FALSE;

    double durationSec = 0.0;
    if (m_loopFrames > 0 && m_sampleRate > 0)
        durationSec = static_cast<double>(m_loopFrames) / m_sampleRate;

    FileInfo->BeginUpdate();

    // duration
    FileInfo->SetValueAsFloat(AIMP_FILEINFO_PROPID_DURATION,
                              static_cast<float>(durationSec));

    // show original samplerate from file header (fallback to playback SR)
    int displayRate = (m_sourceSampleRate > 0) ? m_sourceSampleRate : m_sampleRate;
    if (displayRate > 0)
        FileInfo->SetValueAsInt32(AIMP_FILEINFO_PROPID_SAMPLERATE, displayRate);

    if (m_channels > 0)
        FileInfo->SetValueAsInt32(AIMP_FILEINFO_PROPID_CHANNELS, m_channels);

    // Creator metadata from REXCreatorInfo, if present
    auto setWideStringProp = [&](int propId, const std::wstring& value)
    {
        if (value.empty() || !m_core)
            return;

        IAIMPString* s = nullptr;
        if (SUCCEEDED(m_core->CreateObject(IID_IAIMPString, (void**)&s)))
        {
            s->SetData(const_cast<wchar_t*>(value.c_str()),
                       static_cast<int>(value.length()));
            FileInfo->SetValueAsObject(propId, s);
            s->Release();
        }
    };

    setWideStringProp(AIMP_FILEINFO_PROPID_ARTIST, m_creatorName);
    setWideStringProp(AIMP_FILEINFO_PROPID_COPYRIGHT, m_creatorCopyright);
    setWideStringProp(AIMP_FILEINFO_PROPID_URL, m_creatorURL);
    setWideStringProp(AIMP_FILEINFO_PROPID_COMPOSER, m_creatorName);
    setWideStringProp(AIMP_FILEINFO_PROPID_ALBUMARTIST, m_creatorName);

    // Combine free text + email into the comment field so it shows up in AIMP UI.
    if (!m_creatorFreeText.empty() || !m_creatorEmail.empty())
    {
        std::wstring comment = m_creatorFreeText;
        if (!m_creatorEmail.empty())
        {
            if (!comment.empty())
                comment.append(L"\n");
            comment.append(L"Contact: ");
            comment.append(m_creatorEmail);
        }
        setWideStringProp(AIMP_FILEINFO_PROPID_COMMENT, comment);
    }

    // bitrate & file size (compressed)
    if (m_stream)
    {
        INT64 fileSize = m_stream->GetSize();
        if (fileSize > 0)
        {
            FileInfo->SetValueAsInt64(AIMP_FILEINFO_PROPID_FILESIZE, fileSize);

            if (durationSec > 0.0)
            {
                double bitrateBps  = (static_cast<double>(fileSize) * 8.0) / durationSec;
                int    bitrateKbps = static_cast<int>(bitrateBps / 1000.0 + 0.5);
                if (bitrateKbps > 0)
                    FileInfo->SetValueAsInt32(AIMP_FILEINFO_PROPID_BITRATE, bitrateKbps);
            }
        }
    }

    // BPM from header tempo
    if (m_hasTempoFromFile && m_previewTempo > 0)
    {
        int bpm = static_cast<int>(m_previewTempo / 1000); // 1/1000 BPM
        if (bpm > 0)
            FileInfo->SetValueAsInt32(AIMP_FILEINFO_PROPID_BPM, bpm);
    }

    FileInfo->EndUpdate();
    return TRUE;
}

BOOL WINAPI Rx2Decoder::GetStreamInfo(int *SampleRate, int *Channels, int *SampleFormat)
{
    if (!m_isValid)
        return FALSE;

    if (SampleRate)
        *SampleRate = m_sampleRate;   // playback SR = original SR now

    if (Channels)
        *Channels = m_channels;

    if (SampleFormat)
        *SampleFormat = AIMP_DECODER_SAMPLEFORMAT_32BITFLOAT;

    return TRUE;
}

BOOL WINAPI Rx2Decoder::IsSeekable()
{
    return m_isValid ? TRUE : FALSE;
}

BOOL WINAPI Rx2Decoder::IsRealTimeStream()
{
    return FALSE;
}

INT64 WINAPI Rx2Decoder::GetAvailableData()
{
    if (!m_isValid || m_channels <= 0)
        return 0;

    const int bytesPerSample = 4;
    const INT64 framesLeft   = m_totalSamples - m_positionSamples;
    if (framesLeft <= 0)
        return 0;

    return FramesToBytes(framesLeft, m_channels, bytesPerSample);
}

INT64 WINAPI Rx2Decoder::GetSize()
{
    if (!m_isValid || m_channels <= 0)
        return 0;

    const int bytesPerSample = 4;
    return FramesToBytes(m_totalSamples, m_channels, bytesPerSample);
}

INT64 WINAPI Rx2Decoder::GetPosition()
{
    if (!m_isValid || m_channels <= 0)
        return 0;

    const int bytesPerSample = 4;
    return FramesToBytes(m_positionSamples, m_channels, bytesPerSample);
}

BOOL WINAPI Rx2Decoder::SetPosition(const INT64 Value)
{
    if (!m_isValid || m_totalSamples <= 0 || m_channels <= 0)
        return FALSE;

    const int  channels       = m_channels;
    const int  bytesPerSample = 4;

    INT64 targetFrame = BytesToFrames(Value, channels, bytesPerSample);
    if (targetFrame < 0)
        targetFrame = 0;
    if (targetFrame > m_totalSamples)
        targetFrame = m_totalSamples;

    m_positionSamples = targetFrame;
    return TRUE;
}

int WINAPI Rx2Decoder::Read(void *Buffer, int Count)
{
    if (!Buffer || !m_isValid || m_totalSamples <= 0 || m_channels <= 0)
        return 0;

    const int bytesPerSample = 4;
    const int channels       = m_channels;
    const int frameSize      = channels * bytesPerSample;

    int requestedFrames = Count / frameSize;
    if (requestedFrames <= 0)
        return 0;

    INT64 framesLeft = m_totalSamples - m_positionSamples;
    if (framesLeft <= 0)
        return 0;

    if (requestedFrames > framesLeft)
        requestedFrames = static_cast<int>(framesLeft);

    float *out = static_cast<float*>(Buffer);

    const float *src = m_pcmData.data()
                     + static_cast<size_t>(m_positionSamples) * channels;

    size_t samplesToCopy = static_cast<size_t>(requestedFrames) * channels;
    for (size_t i = 0; i < samplesToCopy; ++i)
        out[i] = src[i];

    m_positionSamples += requestedFrames;

    const int bytesReturned = requestedFrames * frameSize;
    if (bytesReturned > 0)
        m_bytesServed += bytesReturned;

    return bytesReturned;
}
