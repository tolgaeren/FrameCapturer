#include "pch.h"
#include "fcInternal.h"

#ifdef _WIN32
#include "Foundation/fcFoundation.h"
#include "GraphicsDevice/fcGraphicsDevice.h"
#include "fcMP4Context.h"

#include <Windows.h>
#include <VersionHelpers.h>
#include <codecapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <Mfreadwrite.h>
#include <Strmif.h>
#include <wrl/client.h>

#pragma comment(lib, "mfuuid")

using Microsoft::WRL::ComPtr;


class MFInitializer
{
public:
    MFInitializer();
    ~MFInitializer();
};

class fcMP4ContextWMF : public fcIMP4Context
{
public:
    using VideoBuffer   = Buffer;
    using VideoBuffers  = SharedResources<VideoBuffer>;

    using AudioBuffer   = RawVector<float>;
    using AudioBuffers  = SharedResources<AudioBuffer>;


    fcMP4ContextWMF(const fcMP4Config &conf, fcIGraphicsDevice *dev, const char *path);
    ~fcMP4ContextWMF();
    bool isValid() const override;

    const char* getAudioEncoderInfo() override;
    const char* getVideoEncoderInfo() override;

    void addOutputStream(fcStream *s) override;

    bool addVideoFrameTexture(void *tex, fcPixelFormat fmt, fcTime timestamp) override;
    bool addVideoFramePixels(const void *pixels, fcPixelFormat fmt, fcTime timestamp) override;
    bool addVideoFramePixelsImpl(const void *pixels, fcPixelFormat fmt, fcTime timestamp);

    bool addAudioSamples(const float *samples, int num_samples) override;
    void writeOutAudioSamples(double timestamp);
    bool addAudioSamplesImpl(const float *samples, int num_samples);


private:
    bool initializeSinkWriter(const char *path);

    fcMP4Config         m_conf;
    fcIGraphicsDevice   *m_gdev = nullptr;

    TaskQueue           m_video_tasks;
    VideoBuffers        m_video_buffers;
    Buffer              m_rgba_image;
    I420Image           m_i420_image;
    int                 m_frame_count = 0;
    double              m_last_timestamp = 0.0;

    TaskQueue           m_audio_tasks;
    AudioBuffer         m_audio_samples;
    AudioBuffers        m_audio_buffers;
    uint64_t            m_audio_written_samples = 0;

    ComPtr<IMFSinkWriter> m_mf_writer;
    DWORD               m_mf_video_index = 0;
    DWORD               m_mf_audio_index = 0;
};



static HMODULE g_MFPlat;
static HMODULE g_MFReadWrite;
static HRESULT(STDAPICALLTYPE *MFStartup_)(ULONG Version, DWORD dwFlags);
static HRESULT(STDAPICALLTYPE *MFShutdown_)();
static HRESULT(STDAPICALLTYPE *MFCreateMemoryBuffer_)(DWORD cbMaxLength, IMFMediaBuffer **ppBuffer);
static HRESULT(STDAPICALLTYPE *MFCreateSample_)(IMFSample **ppIMFSample);
static HRESULT(STDAPICALLTYPE *MFCreateAttributes_)(IMFAttributes** ppMFAttributes, UINT32 cInitialSize);
static HRESULT(STDAPICALLTYPE *MFCreateMediaType_)(IMFMediaType** ppMFType);
static HRESULT(STDAPICALLTYPE *MFCreateSinkWriterFromURL_)(LPCWSTR pwszOutputURL, IMFByteStream *pByteStream, IMFAttributes *pAttributes, IMFSinkWriter **ppSinkWriter);

static LazyInstance<MFInitializer> g_MFInitializer;


MFInitializer::MFInitializer()
{
    if (!IsWindows7OrGreater()) { return; }

    g_MFPlat = ::LoadLibraryA("MFPlat.DLL");
    g_MFReadWrite = ::LoadLibraryA("MFReadWrite.dll");
    if (g_MFPlat && g_MFReadWrite) {
        bool ok = true;
#define Import(Name) (void*&)Name##_ = ::GetProcAddress(g_MFPlat, #Name); if(!Name##_) { ok = false; }
        Import(MFStartup);
        Import(MFShutdown);
        Import(MFCreateMemoryBuffer);
        Import(MFCreateSample);
        Import(MFCreateAttributes);
        Import(MFCreateMediaType);
#undef Import
#define Import(Name) (void*&)Name##_ = ::GetProcAddress(g_MFReadWrite, #Name); if(!Name##_) { ok = false; }
        Import(MFCreateSinkWriterFromURL);
#undef Import

        if (ok) {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            MFStartup_(MF_VERSION, MFSTARTUP_LITE);
        }
        else {
            ::FreeLibrary(g_MFPlat); g_MFPlat = nullptr;
            ::FreeLibrary(g_MFReadWrite); g_MFReadWrite = nullptr;
        }
    }
}

MFInitializer::~MFInitializer()
{
    //// this can cause trouble.

    //if (g_MFPlat && g_MFReadWrite) {
    //    MFShutdown_();
    //    CoUninitialize();
    //}
}


fcMP4ContextWMF::fcMP4ContextWMF(const fcMP4Config &conf, fcIGraphicsDevice *dev, const char *path)
    : m_conf(conf)
    , m_gdev(dev)
{
    g_MFInitializer.get();
    m_conf.video_max_tasks = std::max<int>(m_conf.video_max_tasks, 1);
    m_conf.audio_max_tasks = std::max<int>(m_conf.audio_max_tasks, 1);

    initializeSinkWriter(path);
}

fcMP4ContextWMF::~fcMP4ContextWMF()
{
    writeOutAudioSamples(m_last_timestamp);
    m_video_tasks.wait();
    m_audio_tasks.wait();

    if (m_mf_writer) {
        m_mf_writer->Finalize();
        m_mf_writer.Reset();
    }
}

bool fcMP4ContextWMF::isValid() const
{
    return m_mf_writer != nullptr;
}

const char* fcMP4ContextWMF::getAudioEncoderInfo()
{
    return nullptr;
}

const char* fcMP4ContextWMF::getVideoEncoderInfo()
{
    return nullptr;
}

void fcMP4ContextWMF::addOutputStream(fcStream *s)
{
    // do nothing
}


static inline HRESULT SetAttributeU32(ComPtr<ICodecAPI>& codec, const GUID& guid, UINT32 value)
{
    VARIANT val;
    val.vt = VT_UI4;
    val.uintVal = value;
    return codec->SetValue(&guid, &val);
}

bool fcMP4ContextWMF::initializeSinkWriter(const char *path)
{
    if (!g_MFPlat || !g_MFReadWrite) { return false; }

    ComPtr<IMFSinkWriter> pSinkWriter;
    HRESULT hr;

    {
        ComPtr<IMFAttributes> pAttributes;
        MFCreateAttributes_(&pAttributes, 1);
        if (pAttributes) {
            hr = pAttributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);

            std::wstring wpath;
            wpath.resize(strlen(path)+1);
            std::mbstowcs(&wpath[0], path, wpath.size());
            hr = MFCreateSinkWriterFromURL_(wpath.data(), nullptr, pAttributes.Get(), &pSinkWriter);
        }
    }
    if (!pSinkWriter) { return false; }

    // Set the video output media type.
    if(m_conf.video) {
        ComPtr<IMFMediaType> pVideoOutMediaType;
        ComPtr<IMFMediaType> pVideoInputMediaType;
        MFCreateMediaType_(&pVideoOutMediaType);
        MFCreateMediaType_(&pVideoInputMediaType);

        if (!pVideoOutMediaType || !pVideoInputMediaType) {
            m_conf.video = false;
        }
        else {
            pVideoOutMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            pVideoOutMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
            pVideoOutMediaType->SetUINT32(MF_MT_AVG_BITRATE, m_conf.video_target_bitrate);
            pVideoOutMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            MFSetAttributeSize(pVideoOutMediaType.Get(), MF_MT_FRAME_SIZE, m_conf.video_width, m_conf.video_height);
            MFSetAttributeRatio(pVideoOutMediaType.Get(), MF_MT_FRAME_RATE, m_conf.video_target_framerate, 1);
            MFSetAttributeRatio(pVideoOutMediaType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
            hr = pSinkWriter->AddStream(pVideoOutMediaType.Get(), &m_mf_video_index);

            pVideoInputMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            pVideoInputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_I420);
            pVideoInputMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            MFSetAttributeSize(pVideoInputMediaType.Get(), MF_MT_FRAME_SIZE, m_conf.video_width, m_conf.video_height);
            MFSetAttributeRatio(pVideoInputMediaType.Get(), MF_MT_FRAME_RATE, m_conf.video_target_framerate, 1);
            MFSetAttributeRatio(pVideoInputMediaType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
            hr = pSinkWriter->SetInputMediaType(m_mf_video_index, pVideoInputMediaType.Get(), nullptr);

            ComPtr<ICodecAPI> encoder;
            pSinkWriter->GetServiceForStream(m_mf_video_index, GUID_NULL, IID_PPV_ARGS(&encoder));
            if (encoder) {
                if (IsWindows8OrGreater()) {
                    pVideoOutMediaType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
                    SetAttributeU32(encoder, CODECAPI_AVEncAdaptiveMode, eAVEncAdaptiveMode_FrameRate);

                    switch (m_conf.video_bitrate_mode) {
                    case fcBitrateMode::CBR:
                        SetAttributeU32(encoder, CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
                        SetAttributeU32(encoder, CODECAPI_AVEncCommonMeanBitRate, m_conf.video_target_bitrate);
                        break;
                    case fcBitrateMode::VBR:
                        SetAttributeU32(encoder, CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_PeakConstrainedVBR);
                        SetAttributeU32(encoder, CODECAPI_AVEncCommonMaxBitRate, m_conf.video_target_bitrate);
                        break;
                    }
                }
                else {
                    pVideoOutMediaType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Main);

                }
            }

            for (int i = 0; i < m_conf.video_max_tasks; ++i) {
                m_video_buffers.emplace();
            }
        }
    }

    // Set the audio output media type.
    if (m_conf.audio) {
        ComPtr<IMFMediaType> pAudioOutMediaType;
        ComPtr<IMFMediaType> pAudioInMediaType;
        MFCreateMediaType_(&pAudioOutMediaType);
        MFCreateMediaType_(&pAudioInMediaType);

        if (!pAudioOutMediaType || !pAudioInMediaType) {
            m_conf.audio = false;
        }
        else {
            pAudioOutMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            pAudioOutMediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
            pAudioOutMediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            pAudioOutMediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, m_conf.audio_sample_rate);
            pAudioOutMediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, m_conf.audio_num_channels);
            pAudioOutMediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, m_conf.audio_target_bitrate / 8);
            pAudioOutMediaType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);
            pAudioOutMediaType->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
            pSinkWriter->AddStream(pAudioOutMediaType.Get(), &m_mf_audio_index);

            pAudioInMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            pAudioInMediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
            pAudioInMediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
            pAudioInMediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, m_conf.audio_sample_rate);
            pAudioInMediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, m_conf.audio_num_channels);
            pAudioInMediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, m_conf.audio_sample_rate * 4 * m_conf.audio_num_channels);
            pAudioInMediaType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4 * m_conf.audio_num_channels);
            pSinkWriter->SetInputMediaType(m_mf_audio_index, pAudioInMediaType.Get(), nullptr);

            for (int i = 0; i < m_conf.audio_max_tasks; ++i) {
                m_audio_buffers.emplace();
            }
        }
    }

    // Tell the sink writer to start accepting data.
    if (!SUCCEEDED(pSinkWriter->BeginWriting())) {
        pSinkWriter.Reset();
        return false;
    }

    // Return the pointer to the caller.
    m_mf_writer = pSinkWriter;
    return m_mf_writer != nullptr;
}


static inline LONGLONG to_hnsec(fcTime t)
{
    return LONGLONG(t * double(10 * 1000 * 1000));
}

bool fcMP4ContextWMF::addVideoFrameTexture(void *tex, fcPixelFormat fmt, fcTime timestamp)
{
    if (!isValid() || !m_conf.video || !tex || !m_gdev) { return false; }

    auto buf = m_video_buffers.acquire();
    size_t psize = fcGetPixelSize(fmt);
    size_t size = m_conf.video_width * m_conf.video_height * psize;
    buf->resize(size);
    if (m_gdev->readTexture(buf->data(), buf->size(), tex, m_conf.video_width, m_conf.video_height, fmt)) {
        m_video_tasks.run([this, buf, fmt, timestamp]() {
            addVideoFramePixelsImpl(buf->data(), fmt, timestamp);
        });
    }
    else {
        return false;
    }

    ++m_frame_count;
    if(m_frame_count % 30 == 0) { writeOutAudioSamples(timestamp); }
    m_last_timestamp = timestamp;
    return true;
}

bool fcMP4ContextWMF::addVideoFramePixels(const void *pixels, fcPixelFormat fmt, fcTime timestamp)
{
    if (!isValid() || !m_conf.video || !pixels) { return false; }

    auto buf = m_video_buffers.acquire();
    size_t psize = fcGetPixelSize(fmt);
    size_t size = m_conf.video_width * m_conf.video_height * psize;
    buf->resize(size);
    memcpy(buf->data(), pixels, size);

    m_video_tasks.run([this, buf, fmt, timestamp]() {
        addVideoFramePixelsImpl(buf->data(), fmt, timestamp);
    });

    ++m_frame_count;
    if (m_frame_count % 30 == 0) { writeOutAudioSamples(timestamp); }
    m_last_timestamp = timestamp;
    return true;
}

bool fcMP4ContextWMF::addVideoFramePixelsImpl(const void *pixels, fcPixelFormat fmt, fcTime timestamp)
{
    const LONGLONG start = to_hnsec(timestamp);
    const LONGLONG duration = to_hnsec(1.0 / m_conf.video_target_framerate);
    const DWORD size = roundup<2>(m_conf.video_width) * roundup<2>(m_conf.video_height);
    const DWORD buffer_size = size + (size >> 2) + (size >> 2);

    // convert image to I420
    AnyToI420(m_i420_image, m_rgba_image, pixels, fmt, m_conf.video_width, m_conf.video_height);
    auto& i420 = m_i420_image.data();


    ComPtr<IMFMediaBuffer> pBuffer;
    ComPtr<IMFSample> pSample;
    MFCreateMemoryBuffer_(buffer_size, &pBuffer);
    MFCreateSample_(&pSample);
    if (!pBuffer || !pSample) { return false; }

    BYTE *pData = nullptr;
    pBuffer->Lock(&pData, nullptr, nullptr);
    memcpy(pData, i420.y, buffer_size);
    pBuffer->Unlock();
    pBuffer->SetCurrentLength(buffer_size);

    pSample->AddBuffer(pBuffer.Get());
    pSample->SetSampleTime(start);
    pSample->SetSampleDuration(duration);
    m_mf_writer->WriteSample(m_mf_video_index, pSample.Get());

    return true;
}

bool fcMP4ContextWMF::addAudioSamples(const float *samples, int num_samples)
{
    if (!isValid() || !m_conf.audio || !samples) { return false; }

    if (m_conf.video) {
        // delay to writeOutAudio()
        m_audio_samples.append(samples, num_samples);
    }
    else {
        auto buf = m_audio_buffers.acquire();
        buf->assign(samples, num_samples);

        m_audio_tasks.run([this, buf, num_samples]() {
            addAudioSamplesImpl(buf->data(), num_samples);
        });
    }

    return true;
}

void fcMP4ContextWMF::writeOutAudioSamples(double timestamp)
{
    uint64_t num_samples = (uint64_t)(std::max<double>(timestamp, 0.0) * (double)(m_conf.audio_sample_rate * m_conf.audio_num_channels));
    uint64_t num_write = num_samples - m_audio_written_samples;
    auto unit = m_conf.audio_sample_rate * m_conf.audio_num_channels / 1000;
    num_write += unit - (num_write % unit);
    num_write = std::min<uint64_t>(num_write, m_audio_samples.size());
    if (num_write == 0) { return; }

    auto buf = m_audio_buffers.acquire();
    buf->assign(m_audio_samples.data(), (int)num_write);
    m_audio_samples.erase(m_audio_samples.begin(), m_audio_samples.begin() + num_write);

    m_audio_tasks.run([this, buf, num_write]() {
        addAudioSamplesImpl(buf->data(), (int)num_write);
    });
}

bool fcMP4ContextWMF::addAudioSamplesImpl(const float *samples, int num_samples)
{
    double timestamp = (double)m_audio_written_samples / (double)(m_conf.audio_sample_rate * m_conf.audio_num_channels);
    double duration = (double)num_samples / (double)(m_conf.audio_sample_rate * m_conf.audio_num_channels);
    const DWORD data_size = num_samples * sizeof(float);

    ComPtr<IMFMediaBuffer> pBuffer;
    ComPtr<IMFSample> pSample;
    MFCreateMemoryBuffer_(data_size, &pBuffer);
    MFCreateSample_(&pSample);
    if (!pBuffer || !pSample) { return false; }

    BYTE *pData = nullptr;
    pBuffer->Lock(&pData, nullptr, nullptr);
    memcpy(pData, samples, data_size);
    pBuffer->Unlock();
    pBuffer->SetCurrentLength(data_size);

    pSample->AddBuffer(pBuffer.Get());
    pSample->SetSampleTime(to_hnsec(timestamp));
    pSample->SetSampleDuration(to_hnsec(duration));
    m_mf_writer->WriteSample(m_mf_audio_index, pSample.Get());

    m_audio_written_samples += num_samples;
    return true;
}


fcIMP4Context* fcMP4OSCreateContextImpl(fcMP4Config &conf, fcIGraphicsDevice *dev, const char *path)
{
    auto ret = new fcMP4ContextWMF(conf, dev, path);
    if (!ret->isValid()) {
        delete ret;
        ret = nullptr;
    }
    return ret;
}

bool fcMP4OSIsSupportedImpl()
{
    g_MFInitializer.get();
    return g_MFPlat && g_MFReadWrite;
}

#endif // _WIN32
