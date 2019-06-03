#include "pch.h"
#include "TestCommon.h"


// custom stream functions (just a wrapper of FILE)
static size_t tellp(void *f) { return ftell((FILE*)f); }
static void   seekp(void *f, size_t pos) { fseek((FILE*)f, (long)pos, SEEK_SET); }
static size_t write(void *f, const void *data, size_t len) { return fwrite(data, 1, len, (FILE*)f); }


const int DurationInSeconds = 10;
const int FrameRate = 60;
const int Width = 320;
const int Height = 240;

const int SampleRate = 48000;
const int NumChannels = 1;


static void WriteMovieData(fcIMP4Context *ctx)
{
    // add video frames
    std::thread video_thread = std::thread([&]() {
        RawVector<RGBAu8> video_frame(Width * Height);
        fcTime t = 0;
        for (int i = 0; i < DurationInSeconds * FrameRate; ++i) {
            CreateVideoData(&video_frame[0], Width, Height, i);
            fcMP4AddVideoFramePixels(ctx, &video_frame[0], fcPixelFormat_RGBAu8, t);
            t += 1.0 / FrameRate;
        }
    });

    // add audio frames
    std::thread audio_thread = std::thread([&]() {
        RawVector<float> audio_sample(SampleRate * NumChannels);
        fcTime t = 0;
        for (int i = 0; i < DurationInSeconds; ++i) {
            CreateAudioData(&audio_sample[0], (int)audio_sample.size(), i, 1.0f);
            fcMP4AddAudioSamples(ctx, &audio_sample[0], (int)audio_sample.size());
            t += 1.0;
        }
    });

    // wait
    video_thread.join();
    audio_thread.join();
}


void MP4Test(int video_encoder, int audio_encoder, const char *filename)
{
    fcMP4Config conf;
    conf.video_width = Width;
    conf.video_height = Height;
    conf.video_bitrate_mode = fcBitrateMode::VBR;
    conf.video_target_bitrate = 256000;
    conf.video_flags = video_encoder;
    conf.audio_sample_rate = SampleRate;
    conf.audio_num_channels = NumChannels;
    conf.audio_target_bitrate = 128000;
    conf.audio_flags = audio_encoder;


    printf("MP4Test (%s) begin\n", filename);

    fcStream* fstream = fcCreateFileStream(filename);
    fcIMP4Context *ctx = fcMP4CreateContext(&conf);
    if (!ctx) {
        printf("  Failed to create context. Possibly H264 or AAC encoder is not available.\n");
    }
    fcMP4AddOutputStream(ctx, fstream);
    WriteMovieData(ctx);
    fcReleaseContext(ctx);
    fcReleaseStream(fstream);

    printf("MP4Test (%s) end\n", filename);
}

void MP4TestOSProvidedEncoder(const char *filename)
{
    fcMP4Config conf;
    conf.video_width = Width;
    conf.video_height = Height;
    conf.video_bitrate_mode = fcBitrateMode::VBR;
    conf.video_target_bitrate = 256000;
    conf.audio_sample_rate = SampleRate;
    conf.audio_num_channels = NumChannels;
    conf.audio_target_bitrate = 128000;

    printf("MP4Test (%s) begin\n", filename);

    fcIMP4Context *ctx = fcMP4OSCreateContext(&conf, "WMF.mp4");
    WriteMovieData(ctx);
    fcReleaseContext(ctx);

    printf("MP4Test (%s) end\n", filename);
}


void MP4Test()
{
    if(!fcMP4OSIsSupported()) {
        printf("MP4Test: OS-provided mp4 encoder is not available\n");
    }
    else {
        MP4TestOSProvidedEncoder("OS.mp4");
    }

    if (!fcMP4IsSupported()) {
        printf("MP4Test: mp4 is not supported\n");
    }
    else {
        MP4Test(fcMP4_H264NVIDIA, 0, "NVIDIA.mp4");
        MP4Test(fcMP4_H264AMD, 0, "AMD.mp4");
        MP4Test(fcMP4_H264IntelHW, 0, "IntelHW.mp4");
        MP4Test(fcMP4_H264IntelSW, 0, "IntelSW.mp4");
        MP4Test(fcMP4_H264OpenH264, fcMP4_AACFAAC, "OpenH264.mp4");
    }
}
