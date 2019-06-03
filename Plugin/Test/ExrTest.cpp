#include "pch.h"
#include "TestCommon.h"

template<class T>
void ExrTestImpl(fcIExrContext *ctx, const char *filename)
{
    const int Width = 320;
    const int Height = 240;
    int channels = GetPixelFormat<T>::value & fcPixelFormat_ChannelMask;
    const char *channel_names[] = { "R", "G", "B", "A" };

    RawVector<T> video_frame(Width * Height);
    CreateVideoData(&video_frame[0], Width, Height, 0);
    fcExrBeginImage(ctx, filename, Width, Height);
    for (int i = 0; i < channels; ++i) {
        fcExrAddLayerPixels(ctx, &video_frame[0], GetPixelFormat<T>::value, i, channel_names[i]);
    }
    fcExrEndImage(ctx);
}

void ExrTest()
{
    if (!fcExrIsSupported()) {
        printf("ExrTest: exr is not supported\n");
        return;
    }

    printf("ExrTest begin\n");

    fcIExrContext *ctx = fcExrCreateContext();
    ExrTestImpl<RGBAu8 >(ctx, "RGBAu8.exr");
    ExrTestImpl<RGBAf16>(ctx, "RGBAf16.exr");
    ExrTestImpl<RGBAf32>(ctx, "RGBAf32.exr");
    fcReleaseContext(ctx);

    printf("ExrTest end\n");
}
