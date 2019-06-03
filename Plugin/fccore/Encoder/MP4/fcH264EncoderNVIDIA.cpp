#include "pch.h"
#include "fcInternal.h"
#include "Foundation/fcFoundation.h"
#include "GraphicsDevice/fcGraphicsDevice.h"
#include "fcMP4Internal.h"
#include "fcH264Encoder.h"

#ifdef fcSupportH264_NVIDIA
#include "NVENC/nvEncodeAPI.h"
#if defined(_WIN32)
    #if defined(_M_AMD64)
        #define NVEncoderDLL "nvEncodeAPI64.dll"
    #elif defined(_M_IX86)
        #define NVEncoderDLL "nvEncodeAPI.dll"
    #endif
#elif defined(__linux__)
    #define NVEncoderDLL "libnvidia-encode.so.1"
#endif


class fcH264EncoderNVIDIA : public fcIH264Encoder
{
public:
    fcH264EncoderNVIDIA(const fcH264EncoderConfig& conf, void *device, fcHWEncoderDeviceType type);
    ~fcH264EncoderNVIDIA() override;
    const char* getEncoderInfo() override;
    bool encode(fcH264Frame& dst, const void *image, fcPixelFormat fmt, fcTime timestamp, bool force_keyframe) override;
    bool flush(fcH264Frame& dst) override;

    bool isValid() const;

private:
    fcH264EncoderConfig m_conf;
    void *m_encoder = nullptr;
    NV_ENC_CREATE_INPUT_BUFFER m_input;
    NV_ENC_CREATE_BITSTREAM_BUFFER m_output;

    Buffer m_rgba_image;
    NV12Image m_nv12_image;
};



static module_t g_mod_nvenc;
static NV_ENCODE_API_FUNCTION_LIST nvenc;

static bool LoadNVENCModule()
{
    if (nvenc.nvEncOpenEncodeSession != nullptr) { return true; }

    NVENCSTATUS stat;
    g_mod_nvenc = DLLLoad(NVEncoderDLL);
    if (g_mod_nvenc) {
        NVENCSTATUS(NVENCAPI *NvEncodeAPICreateInstance_)(NV_ENCODE_API_FUNCTION_LIST *functionList);
        (void*&)NvEncodeAPICreateInstance_ = DLLGetSymbol(g_mod_nvenc, "NvEncodeAPICreateInstance");
        if (NvEncodeAPICreateInstance_) {
            nvenc.version = NV_ENCODE_API_FUNCTION_LIST_VER;
            stat = NvEncodeAPICreateInstance_(&nvenc);
        }
    }
    return nvenc.nvEncOpenEncodeSession != nullptr;
}


fcH264EncoderNVIDIA::fcH264EncoderNVIDIA(const fcH264EncoderConfig& conf, void *device, fcHWEncoderDeviceType type)
    : m_conf(conf)
{
    if (!LoadNVENCModule()) { return; }

    NVENCSTATUS stat;
    {
        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
        params.apiVersion = NVENCAPI_VERSION;
        params.device = device;
        switch (type) {
        case fcHWEncoderDeviceType::D3D9:
        case fcHWEncoderDeviceType::D3D10:
        case fcHWEncoderDeviceType::D3D11:
        case fcHWEncoderDeviceType::D3D12:
            params.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
            break;
        case fcHWEncoderDeviceType::CUDA:
            params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
            break;
        }
        stat = nvenc.nvEncOpenEncodeSessionEx(&params, &m_encoder);
        if (!m_encoder) {
            return;
        }
    }

    std::vector<GUID> encode_guilds;
    std::vector<GUID> profile_guilds;
    std::vector<GUID> preset_guilds;
    {
        uint32_t num_encode_guilds = 0;
        uint32_t num_profile_guilds = 0;
        uint32_t num_preset_guilds = 0;

        nvenc.nvEncGetEncodeGUIDCount(m_encoder, &num_encode_guilds);
        encode_guilds.resize(num_encode_guilds);
        nvenc.nvEncGetEncodeGUIDs(m_encoder, encode_guilds.data(), num_encode_guilds, &num_encode_guilds);

        nvenc.nvEncGetEncodeProfileGUIDCount(m_encoder, NV_ENC_CODEC_H264_GUID, &num_profile_guilds);
        profile_guilds.resize(num_profile_guilds);
        nvenc.nvEncGetEncodeProfileGUIDs(m_encoder, NV_ENC_CODEC_H264_GUID, profile_guilds.data(), num_profile_guilds, &num_profile_guilds);

        nvenc.nvEncGetEncodePresetCount(m_encoder, NV_ENC_CODEC_H264_GUID, &num_preset_guilds);
        preset_guilds.resize(num_preset_guilds);
        nvenc.nvEncGetEncodePresetGUIDs(m_encoder, NV_ENC_CODEC_H264_GUID, preset_guilds.data(), num_preset_guilds, &num_preset_guilds);
    }

    {
        NV_ENC_INITIALIZE_PARAMS params = { 0 };
        params.version = NV_ENC_INITIALIZE_PARAMS_VER;
        params.encodeGUID = NV_ENC_CODEC_H264_GUID;
        params.presetGUID = NV_ENC_PRESET_DEFAULT_GUID;
        params.encodeWidth = conf.width;
        params.encodeHeight = conf.height;
        params.darWidth = conf.width;
        params.darHeight = conf.height;
        params.frameRateNum = m_conf.target_framerate;
        params.frameRateDen = 1;
        params.enablePTD = 1;

        NV_ENC_PRESET_CONFIG preset_config = { 0 };
        preset_config.version = NV_ENC_PRESET_CONFIG_VER;
        preset_config.presetCfg.version = NV_ENC_CONFIG_VER;
        stat = nvenc.nvEncGetEncodePresetConfig(m_encoder, params.encodeGUID, params.presetGUID, &preset_config);

        NV_ENC_CONFIG encode_config = { 0 };
        encode_config.version = NV_ENC_CONFIG_VER;
        memcpy(&encode_config, &preset_config.presetCfg, sizeof(NV_ENC_CONFIG));
        encode_config.profileGUID = NV_ENC_H264_PROFILE_HIGH_GUID;
        params.encodeConfig = &encode_config;

        stat = nvenc.nvEncInitializeEncoder(m_encoder, &params);
    }

    {
        memset(&m_input, 0, sizeof(m_input));
        m_input.version = NV_ENC_CREATE_INPUT_BUFFER_VER;
        m_input.width = m_conf.width;
        m_input.height = m_conf.height;
        m_input.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
        stat = nvenc.nvEncCreateInputBuffer(m_encoder, &m_input);
    }

    {
        memset(&m_output, 0, sizeof(m_output));
        m_output.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
        m_output.size = roundup<16>(conf.width) * roundup<16>(conf.height) * 2;
        stat = nvenc.nvEncCreateBitstreamBuffer(m_encoder, &m_output);
    }
}

fcH264EncoderNVIDIA::~fcH264EncoderNVIDIA()
{
    if (m_encoder) {
        if (m_input.inputBuffer) {
            nvenc.nvEncDestroyInputBuffer(m_encoder, m_input.inputBuffer);
        }
        if (m_output.bitstreamBuffer) {
            nvenc.nvEncDestroyBitstreamBuffer(m_encoder, m_output.bitstreamBuffer);
        }
        nvenc.nvEncDestroyEncoder(m_encoder);
    }
}

bool fcH264EncoderNVIDIA::isValid() const
{
    return m_encoder != nullptr &&
        m_input.inputBuffer != nullptr &&
        m_output.bitstreamBuffer != nullptr;
}

const char* fcH264EncoderNVIDIA::getEncoderInfo() { return "NVIDIA H264 Encoder"; }

bool fcH264EncoderNVIDIA::encode(fcH264Frame& dst, const void *image, fcPixelFormat fmt, fcTime timestamp, bool force_keyframe)
{
    if (!isValid()) { return false; }

    dst.timestamp = timestamp;

    // convert image to NV12
    AnyToNV12(m_nv12_image, m_rgba_image, image, fmt, m_conf.width, m_conf.height);
    NV12Data data = m_nv12_image.data();

    NVENCSTATUS stat;

    // upload image to input buffer
    {
        NV_ENC_LOCK_INPUT_BUFFER lock_params = { 0 };
        lock_params.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
        lock_params.inputBuffer = m_input.inputBuffer;
        stat = nvenc.nvEncLockInputBuffer(m_encoder, &lock_params);
        memcpy(lock_params.bufferDataPtr, data.y, m_nv12_image.size());
        stat = nvenc.nvEncUnlockInputBuffer(m_encoder, m_input.inputBuffer);
    }


    NV_ENC_PIC_PARAMS params = { 0 };
    params.version = NV_ENC_PIC_PARAMS_VER;
    params.inputBuffer = m_input.inputBuffer;
    params.outputBitstream = m_output.bitstreamBuffer;
    params.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
    params.inputWidth = m_conf.width;
    params.inputHeight = m_conf.height;
    params.completionEvent = 0;
    params.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    params.encodePicFlags = 0;
    if (force_keyframe) {
        params.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEINTRA;
    }
    params.inputTimeStamp = to_usec(timestamp);
    params.inputDuration = to_usec(1.0 / m_conf.target_framerate);

    // encode! 
    stat = nvenc.nvEncEncodePicture(m_encoder, &params);

    // retrieve encoded data
    {
        NV_ENC_LOCK_BITSTREAM lock_params = { 0 };
        lock_params.version = NV_ENC_LOCK_BITSTREAM_VER;
        lock_params.outputBitstream = m_output.bitstreamBuffer;

        stat = nvenc.nvEncLockBitstream(m_encoder, &lock_params);

        dst.data.append((char*)lock_params.bitstreamBufferPtr, lock_params.bitstreamSizeInBytes);
        dst.gatherNALInformation();

        stat = nvenc.nvEncUnlockBitstream(m_encoder, m_output.bitstreamBuffer);
    }

    return true;
}

bool fcH264EncoderNVIDIA::flush(fcH264Frame& dst)
{
    return false;
}

fcIH264Encoder* fcCreateH264EncoderNVIDIA(const fcH264EncoderConfig& conf, void *device, fcHWEncoderDeviceType type)
{
    auto *ret = new fcH264EncoderNVIDIA(conf, device, type);
    if (!ret->isValid()) {
        delete ret;
        ret = nullptr;
    }
    return ret;
}

#else // fcSupportH264_NVIDIA

fcIH264Encoder* fcCreateH264EncoderNVIDIA(const fcH264EncoderConfig& conf, void *device, fcHWEncoderDeviceType type) { return nullptr; }

#endif // fcSupportH264_NVIDIA
