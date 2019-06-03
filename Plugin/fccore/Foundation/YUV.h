#pragma once

#include "Buffer.h"
#include "PixelFormat.h"


// I420

struct I420Data
{
    void *y = nullptr;
    void *u = nullptr;
    void *v = nullptr;
    int pitch_y = 0;
    int pitch_u = 0;
    int pitch_v = 0;
    int height = 0;
};

class I420Image
{
public:
    void resize(int width, int height);
    size_t size() const;
    I420Data& data();
    const I420Data& data() const;

private:
    Buffer m_buffer;
    I420Data m_data;
};

void AnyToI420(I420Image& dst, Buffer& tmp, const void *pixels, fcPixelFormat fmt, int width, int height);


// NV12

struct NV12Data
{
    void *y = nullptr;
    void *uv = nullptr;
    int pitch_y = 0;
    int pitch_uv = 0;
    int height = 0;
};

class NV12Image
{
public:
    void resize(int width, int height);
    size_t size() const;
    NV12Data& data();
    const NV12Data& data() const;

private:
    Buffer m_buffer;
    NV12Data m_data;
};

void RGBAToNV12(NV12Image& dst, const void *rgba_pixels, int width, int height);
void RGBAToNV12(const NV12Data& dst, const void *rgba_pixels, int width, int height);
void AnyToNV12(NV12Image& dst, Buffer& tmp, const void *pixels, fcPixelFormat fmt, int width, int height);
