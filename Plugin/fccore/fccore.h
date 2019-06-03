﻿#pragma once

#ifdef _WIN32
    #ifdef fcImpl
        #define fcAPI extern "C" __declspec(dllexport)
    #else
        #define fcAPI extern "C" __declspec(dllimport)
    #endif
#else
    #define fcAPI extern "C"
#endif

#include <cstdint>

class fcIGraphicsDevice;
using fcTime = double;

enum fcPixelFormat
{
    fcPixelFormat_Unknown = 0,

    fcPixelFormat_ChannelMask = 0xF,
    fcPixelFormat_TypeMask = 0xF << 4,
    fcPixelFormat_Type_f16 = 0x1 << 4,
    fcPixelFormat_Type_f32 = 0x2 << 4,
    fcPixelFormat_Type_u8  = 0x3 << 4,
    fcPixelFormat_Type_i16 = 0x4 << 4,
    fcPixelFormat_Type_i32 = 0x5 << 4,

    fcPixelFormat_Rf16      = fcPixelFormat_Type_f16 | 1,
    fcPixelFormat_RGf16     = fcPixelFormat_Type_f16 | 2,
    fcPixelFormat_RGBf16    = fcPixelFormat_Type_f16 | 3,
    fcPixelFormat_RGBAf16   = fcPixelFormat_Type_f16 | 4,
    fcPixelFormat_Rf32      = fcPixelFormat_Type_f32 | 1,
    fcPixelFormat_RGf32     = fcPixelFormat_Type_f32 | 2,
    fcPixelFormat_RGBf32    = fcPixelFormat_Type_f32 | 3,
    fcPixelFormat_RGBAf32   = fcPixelFormat_Type_f32 | 4,
    fcPixelFormat_Ru8       = fcPixelFormat_Type_u8  | 1,
    fcPixelFormat_RGu8      = fcPixelFormat_Type_u8  | 2,
    fcPixelFormat_RGBu8     = fcPixelFormat_Type_u8  | 3,
    fcPixelFormat_RGBAu8    = fcPixelFormat_Type_u8  | 4,
    fcPixelFormat_Ri16      = fcPixelFormat_Type_i16 | 1,
    fcPixelFormat_RGi16     = fcPixelFormat_Type_i16 | 2,
    fcPixelFormat_RGBi16    = fcPixelFormat_Type_i16 | 3,
    fcPixelFormat_RGBAi16   = fcPixelFormat_Type_i16 | 4,
    fcPixelFormat_Ri32      = fcPixelFormat_Type_i32 | 1,
    fcPixelFormat_RGi32     = fcPixelFormat_Type_i32 | 2,
    fcPixelFormat_RGBi32    = fcPixelFormat_Type_i32 | 3,
    fcPixelFormat_RGBAi32   = fcPixelFormat_Type_i32 | 4,
    fcPixelFormat_I420      = 0x10 << 4,
    fcPixelFormat_NV12      = 0x11 << 4,
};

enum class fcBitrateMode
{
    CBR,
    VBR,
};


// -------------------------------------------------------------
// Foundation
// -------------------------------------------------------------

fcAPI void            fcGfxInitializeOpenGL();
fcAPI void            fcGfxInitializeD3D9(void *device);
fcAPI void            fcGfxInitializeD3D11(void *device);
fcAPI void            fcGfxFinalize();
fcAPI void            fcGfxSync();

fcAPI void            fcSetModulePath(const char *path);
fcAPI const char*     fcGetModulePath();
fcAPI fcTime          fcGetTime(); // current time in seconds


#ifndef fcImpl
struct fcStream;
using fcContextBase = void;
#else
class BinaryStream;
using fcStream = BinaryStream;
class fcContextBase;
#endif
// function types for custom stream
using fcTellp_t = size_t(*)(void *obj);
using fcSeekp_t = void(*)(void *obj, size_t pos);
using fcWrite_t = size_t(*)(void *obj, const void *data, size_t len);

struct fcBufferData
{
    void *data = nullptr;
    size_t size = 0;
};
fcAPI fcStream*       fcCreateFileStream(const char *path);
fcAPI fcStream*       fcCreateMemoryStream();
fcAPI fcStream*       fcCreateCustomStream(void *obj, fcTellp_t tellp, fcSeekp_t seekp, fcWrite_t write);
fcAPI void            fcReleaseStream(fcStream *s);
fcAPI fcBufferData    fcStreamGetBufferData(fcStream *s); // s must be created by fcCreateMemoryStream(), otherwise return {nullptr, 0}.
fcAPI uint64_t        fcStreamGetWrittenSize(fcStream *s);

fcAPI void            fcEnableAsyncReleaseContext(bool v);
fcAPI void            fcWaitAsyncDelete();

fcAPI void            fcReleaseContext(fcContextBase *ctx);
fcAPI void            fcSetOnDeleteCallback(fcContextBase *ctx, void(*cb)(void*), void *param);


// -------------------------------------------------------------
// PNG Exporter
// -------------------------------------------------------------

class fcIPngContext;

enum class fcPngPixelFormat
{
    Auto, // select optimal one for input data
    UInt8,
    UInt16,
};

struct fcPngConfig
{
    fcPngPixelFormat pixel_format = fcPngPixelFormat::Auto;
    int max_tasks = 4;
};

fcAPI bool            fcPngIsSupported();
fcAPI fcIPngContext*  fcPngCreateContext(const fcPngConfig *conf = nullptr);
fcAPI bool            fcPngExportPixels(fcIPngContext *ctx, const char *path, const void *pixels, int width, int height, fcPixelFormat fmt, int num_channels = 0);
fcAPI bool            fcPngExportTexture(fcIPngContext *ctx, const char *path, void *tex, int width, int height, fcPixelFormat fmt, int num_channels = 0);


// -------------------------------------------------------------
// EXR Exporter
// -------------------------------------------------------------

class fcIExrContext;

enum class fcExrPixelFormat
{
    Auto, // select optimal one for input data
    Half,
    Float,
    Int,
};

enum class fcExrCompression
{
    None,
    RLE,
    ZipS, // par-line
    Zip,  // block
    PIZ,
};

struct fcExrConfig
{
    fcExrPixelFormat pixel_format = fcExrPixelFormat::Auto;
    fcExrCompression compression = fcExrCompression::Zip;
    int max_tasks = 4;
};

fcAPI bool            fcExrIsSupported();
fcAPI fcIExrContext*  fcExrCreateContext(const fcExrConfig *conf = nullptr);
fcAPI bool            fcExrBeginImage(fcIExrContext *ctx, const char *path, int width, int height);
fcAPI bool            fcExrAddLayerPixels(fcIExrContext *ctx, const void *pixels, fcPixelFormat fmt, int ch, const char *name);
fcAPI bool            fcExrAddLayerTexture(fcIExrContext *ctx, void *tex, fcPixelFormat fmt, int ch, const char *name);
fcAPI bool            fcExrEndImage(fcIExrContext *ctx);


// -------------------------------------------------------------
// GIF Exporter
// -------------------------------------------------------------

class fcIGifContext;

struct fcGifConfig
{
    int width = 0;
    int height = 0;
    int num_colors = 256;
    int keyframe_interval = 30;
    int max_tasks = 8;
};

fcAPI bool            fcGifIsSupported();
fcAPI fcIGifContext*  fcGifCreateContext(const fcGifConfig *conf);
fcAPI void            fcGifAddOutputStream(fcIGifContext *ctx, fcStream *stream);
// timestamp=-1 is treated as current time.
fcAPI bool            fcGifAddFramePixels(fcIGifContext *ctx, const void *pixels, fcPixelFormat fmt, fcTime timestamp = -1.0);
// timestamp=-1 is treated as current time.
fcAPI bool            fcGifAddFrameTexture(fcIGifContext *ctx, void *tex, fcPixelFormat fmt, fcTime timestamp = -1.0);
// force next frame to update palette
fcAPI void            fcGifForceKeyframe(fcIGifContext *ctx);


// -------------------------------------------------------------
// MP4 Exporter
// -------------------------------------------------------------

class fcIMP4Context;

enum fcMP4VideoFlags
{
    fcMP4_H264NVIDIA    = 1 << 1,
    fcMP4_H264AMD       = 1 << 2,
    fcMP4_H264IntelHW   = 1 << 3,
    fcMP4_H264IntelSW   = 1 << 4,
    fcMP4_H264OpenH264  = 1 << 5,
    fcMP4_H264Mask = fcMP4_H264NVIDIA | fcMP4_H264AMD | fcMP4_H264IntelHW | fcMP4_H264IntelSW | fcMP4_H264OpenH264,
};

enum fcMP4AudioFlags
{
    fcMP4_AACIntel  = 1 << 1,
    fcMP4_AACFAAC   = 1 << 2,
    fcMP4_AACMask = fcMP4_AACIntel | fcMP4_AACFAAC,
};

struct fcMP4Config
{
    bool video = true;
    int video_width = 0;
    int video_height = 0;
    int video_target_framerate = 60;
    fcBitrateMode video_bitrate_mode = fcBitrateMode::VBR;
    int video_target_bitrate = 1024 * 1000;
    int video_flags = fcMP4_H264Mask; // combination of fcMP4VideoFlags
    int video_max_tasks = 4;

    bool audio = true;
    int audio_sample_rate = 48000;
    int audio_num_channels = 2;
    fcBitrateMode audio_bitrate_mode = fcBitrateMode::VBR;
    int audio_target_bitrate = 128 * 1000;
    int audio_flags = fcMP4_AACMask; // combination of fcMP4AudioFlags
    int audio_max_tasks = 4;
};

fcAPI bool            fcMP4IsSupported();
fcAPI bool            fcMP4OSIsSupported();
fcAPI fcIMP4Context*  fcMP4CreateContext(fcMP4Config *conf);
// OS-provided mp4 encoder. in this case video_flags and audio_flags in conf are ignored
fcAPI fcIMP4Context*  fcMP4OSCreateContext(fcMP4Config *conf, const char *out_path);
fcAPI const char*     fcMP4GetVideoEncoderInfo(fcIMP4Context *ctx);
fcAPI const char*     fcMP4GetAudioEncoderInfo(fcIMP4Context *ctx);
fcAPI void            fcMP4AddOutputStream(fcIMP4Context *ctx, fcStream *stream);
// timestamp=-1 is treated as current time.
fcAPI bool            fcMP4AddVideoFramePixels(fcIMP4Context *ctx, const void *pixels, fcPixelFormat fmt, fcTime timestamp = -1.0);
// timestamp=-1 is treated as current time.
fcAPI bool            fcMP4AddVideoFrameTexture(fcIMP4Context *ctx, void *tex, fcPixelFormat fmt, fcTime timestamp = -1.0);
fcAPI bool            fcMP4AddAudioSamples(fcIMP4Context *ctx, const float *samples, int num_samples);



// -------------------------------------------------------------
// WebM Exporter
// -------------------------------------------------------------

class fcIWebMContext;

enum class fcWebMVideoEncoder
{
    VPX_VP8,
    VPX_VP9,
    VPX_VP9LossLess,
};
enum class fcWebMAudioEncoder
{
    Vorbis,
    Opus,
};

struct fcWebMConfig
{
    bool video = true;
    fcWebMVideoEncoder video_encoder = fcWebMVideoEncoder::VPX_VP8;
    int video_width = 0;
    int video_height = 0;
    int video_target_framerate = 60;
    fcBitrateMode video_bitrate_mode = fcBitrateMode::VBR;
    int video_target_bitrate = 1024 * 1000;
    int video_max_tasks = 4;

    bool audio = true;
    fcWebMAudioEncoder audio_encoder = fcWebMAudioEncoder::Vorbis;
    int audio_sample_rate = 48000;
    int audio_num_channels = 2;
    fcBitrateMode audio_bitrate_mode = fcBitrateMode::VBR;
    int audio_target_bitrate = 128 * 1000;
    int audio_max_tasks = 4;
};

fcAPI bool            fcWebMIsSupported();
fcAPI fcIWebMContext* fcWebMCreateContext(fcWebMConfig *conf);
fcAPI void            fcWebMAddOutputStream(fcIWebMContext *ctx, fcStream *stream);
// timestamp=-1 is treated as current time.
fcAPI bool            fcWebMAddVideoFramePixels(fcIWebMContext *ctx, const void *pixels, fcPixelFormat fmt, fcTime timestamp = -1.0);
// timestamp=-1 is treated as current time.
fcAPI bool            fcWebMAddVideoFrameTexture(fcIWebMContext *ctx, void *tex, fcPixelFormat fmt, fcTime timestamp = -1.0);
fcAPI bool            fcWebMAddAudioSamples(fcIWebMContext *ctx, const float *samples, int num_samples);



// -------------------------------------------------------------
// Wave Exporter
// -------------------------------------------------------------

class fcIWaveContext;

struct fcWaveConfig
{
    int sample_rate = 48000;
    int num_channels = 2;
    int bits_per_sample = 16;
    int max_tasks = 4;
};
fcAPI bool            fcWaveIsSupported();
fcAPI fcIWaveContext* fcWaveCreateContext(fcWaveConfig *conf);
fcAPI void            fcWaveAddOutputStream(fcIWaveContext *ctx, fcStream *stream);
fcAPI bool            fcWaveAddAudioSamples(fcIWaveContext *ctx, const float *samples, int num_samples);


// -------------------------------------------------------------
// Ogg Exporter
// -------------------------------------------------------------

class fcIOggContext;

struct fcOggConfig
{
    int sample_rate = 48000;
    int num_channels = 2;
    fcBitrateMode bitrate_mode = fcBitrateMode::VBR;
    int target_bitrate = 128 * 1000;
    int max_tasks = 4;
};
fcAPI bool            fcOggIsSupported();
fcAPI fcIOggContext*  fcOggCreateContext(fcOggConfig *conf);
fcAPI void            fcOggAddOutputStream(fcIOggContext *ctx, fcStream *stream);
fcAPI bool            fcOggAddAudioSamples(fcIOggContext *ctx, const float *samples, int num_samples);


// -------------------------------------------------------------
// Flac Exporter
// -------------------------------------------------------------

class fcIFlacContext;

struct fcFlacConfig
{
    int sample_rate = 48000;
    int num_channels = 2;
    int bits_per_sample = 16;
    int compression_level = 5;
    int block_size = 0;
    bool verify = false;
    int max_tasks = 4;
};
fcAPI bool            fcFlacIsSupported();
fcAPI fcIFlacContext* fcFlacCreateContext(fcFlacConfig *conf);
fcAPI void            fcFlacAddOutputStream(fcIFlacContext *ctx, fcStream *stream);
fcAPI bool            fcFlacAddAudioSamples(fcIFlacContext *ctx, const float *samples, int num_samples);


