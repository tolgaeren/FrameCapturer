﻿#include "pch.h"
#include "fcInternal.h"
#include "Foundation/PixelFormat.h"

#ifdef fcSupportD3D9
#include "fcGraphicsDevice.h"
#include <d3d9.h>
const int fcD3D9MaxStagingTextures = 32;

class fcGraphicsDeviceD3D9 : public fcIGraphicsDevice
{
public:
    fcGraphicsDeviceD3D9(void *device);
    ~fcGraphicsDeviceD3D9();
    void* getDevicePtr() override;
    fcGfxDeviceType getDeviceType() override;
    void sync() override;
    bool readTexture(void *o_buf, size_t bufsize, void *tex, int width, int height, fcPixelFormat format) override;
    bool writeTexture(void *o_tex, int width, int height, fcPixelFormat format, const void *buf, size_t bufsize) override;

private:
    void clearStagingTextures();
    IDirect3DSurface9* findOrCreateStagingTexture(int width, int height, fcPixelFormat format);

private:
    IDirect3DDevice9 *m_device;
    IDirect3DQuery9 *m_query_event;
    std::map<uint64_t, IDirect3DSurface9*> m_staging_textures;
};


fcIGraphicsDevice* fcCreateGraphicsDeviceD3D9(void *device)
{
    return new fcGraphicsDeviceD3D9(device);
}


fcGraphicsDeviceD3D9::fcGraphicsDeviceD3D9(void *device)
    : m_device((IDirect3DDevice9*)device)
    , m_query_event(nullptr)
{
    if (m_device != nullptr)
    {
        m_device->CreateQuery(D3DQUERYTYPE_EVENT, &m_query_event);
    }
}

fcGraphicsDeviceD3D9::~fcGraphicsDeviceD3D9()
{
    clearStagingTextures();
}

void* fcGraphicsDeviceD3D9::getDevicePtr() { return m_device; }
fcGfxDeviceType fcGraphicsDeviceD3D9::getDeviceType() { return fcGfxDeviceType::D3D9; }


void fcGraphicsDeviceD3D9::clearStagingTextures()
{
    for (auto& pair : m_staging_textures)
    {
        pair.second->Release();
    }
    m_staging_textures.clear();
}



static D3DFORMAT fcGetInternalFormatD3D9(fcPixelFormat fmt)
{
    switch (fmt)
    {
    case fcPixelFormat_RGBAu8:    return D3DFMT_A8R8G8B8;

    case fcPixelFormat_RGBAf16:  return D3DFMT_A16B16G16R16F;
    case fcPixelFormat_RGf16:    return D3DFMT_G16R16F;
    case fcPixelFormat_Rf16:     return D3DFMT_R16F;

    case fcPixelFormat_RGBAf32: return D3DFMT_A32B32G32R32F;
    case fcPixelFormat_RGf32:   return D3DFMT_G32R32F;
    case fcPixelFormat_Rf32:    return D3DFMT_R32F;
    }
    return D3DFMT_UNKNOWN;
}

IDirect3DSurface9* fcGraphicsDeviceD3D9::findOrCreateStagingTexture(int width, int height, fcPixelFormat format)
{
    if (m_staging_textures.size() >= fcD3D9MaxStagingTextures) {
        clearStagingTextures();
    }

    D3DFORMAT internal_format = fcGetInternalFormatD3D9(format);
    if (internal_format == D3DFMT_UNKNOWN) { return nullptr; }

    uint64_t hash = width + (height << 16) + ((uint64_t)internal_format << 32);
    {
        auto it = m_staging_textures.find(hash);
        if (it != m_staging_textures.end())
        {
            return it->second;
        }
    }

    IDirect3DSurface9 *ret = nullptr;
    HRESULT hr = m_device->CreateOffscreenPlainSurface(width, height, internal_format, D3DPOOL_SYSTEMMEM, &ret, NULL);
    if (SUCCEEDED(hr))
    {
        m_staging_textures.insert(std::make_pair(hash, ret));
    }
    return ret;
}


template<class T>
struct RGBA
{
    T r,g,b,a;
};

template<class T>
inline void BGRA_RGBA_conversion(RGBA<T> *data, int num_pixels)
{
    for (int i = 0; i < num_pixels; ++i) {
        std::swap(data[i].r, data[i].b);
    }
}

template<class T>
inline void copy_with_BGRA_RGBA_conversion(RGBA<T> *dst, const RGBA<T> *src, int num_pixels)
{
    for (int i = 0; i < num_pixels; ++i) {
        RGBA<T>       &d = dst[i];
        const RGBA<T> &s = src[i];
        d.r = s.b;
        d.g = s.g;
        d.b = s.r;
        d.a = s.a;
    }
}

void fcGraphicsDeviceD3D9::sync()
{
    m_query_event->Issue(D3DISSUE_END);
    auto hr = m_query_event->GetData(nullptr, 0, D3DGETDATA_FLUSH);
    if (hr != S_OK) {
        fcDebugLog("fcGraphicsDeviceD3D9::sync(): GetData() failed\n");
    }
}

bool fcGraphicsDeviceD3D9::readTexture(void *o_buf, size_t bufsize, void *tex_, int width, int height, fcPixelFormat format)
{
    HRESULT hr;
    IDirect3DTexture9 *tex = (IDirect3DTexture9*)tex_;

    // D3D11 と同様 render target の内容は CPU からはアクセス不可能になっている。
    // staging texture を用意してそれに内容を移し、CPU はそれ経由でデータを読む。
    IDirect3DSurface9 *surf_dst = findOrCreateStagingTexture(width, height, format);
    if (surf_dst == nullptr) { return false; }

    IDirect3DSurface9* surf_src = nullptr;
    hr = tex->GetSurfaceLevel(0, &surf_src);
    if (FAILED(hr)){ return false; }

    sync();

    bool ret = false;
    hr = m_device->GetRenderTargetData(surf_src, surf_dst);
    if (SUCCEEDED(hr))
    {
        D3DLOCKED_RECT locked;
        hr = surf_dst->LockRect(&locked, nullptr, D3DLOCK_READONLY);
        if (SUCCEEDED(hr))
        {
            char *wpixels = (char*)o_buf;
            int wpitch = width * fcGetPixelSize(format);
            const char *rpixels = (const char*)locked.pBits;
            int rpitch = locked.Pitch;

            // D3D11 と同様表向き解像度と内部解像度が違うケースを考慮
            // (しかし、少なくとも手元の環境では常に wpitch == rpitch っぽい)
            if (wpitch == rpitch)
            {
                memcpy(wpixels, rpixels, bufsize);
            }
            else
            {
                for (int i = 0; i < height; ++i)
                {
                    memcpy(wpixels, rpixels, wpitch);
                    wpixels += wpitch;
                    rpixels += rpitch;
                }
            }
            surf_dst->UnlockRect();

            // D3D9 の ARGB32 のピクセルの並びは BGRA になっているので並べ替える
            if (format == fcPixelFormat_RGBAu8) {
                BGRA_RGBA_conversion((RGBA<uint8_t>*)o_buf, int(bufsize / 4));
            }
            ret = true;
        }
    }

    surf_src->Release();
    return ret;
}

bool fcGraphicsDeviceD3D9::writeTexture(void *o_tex, int width, int height, fcPixelFormat format, const void *buf, size_t bufsize)
{
    int psize = fcGetPixelSize(format);
    int pitch = psize * width;
    const size_t num_pixels = bufsize / psize;

    HRESULT hr;
    IDirect3DTexture9 *tex = (IDirect3DTexture9*)o_tex;

    // D3D11 と違い、D3D9 では書き込みも staging texture を経由する必要がある。
    IDirect3DSurface9 *surf_src = findOrCreateStagingTexture(width, height, format);
    if (surf_src == nullptr) { return false; }

    IDirect3DSurface9* surf_dst = nullptr;
    hr = tex->GetSurfaceLevel(0, &surf_dst);
    if (FAILED(hr)){ return false; }

    bool ret = false;
    D3DLOCKED_RECT locked;
    hr = surf_src->LockRect(&locked, nullptr, D3DLOCK_DISCARD);
    if (SUCCEEDED(hr))
    {
        const char *rpixels = (const char*)buf;
        int rpitch = psize * width;
        char *wpixels = (char*)locked.pBits;
        int wpitch = locked.Pitch;

        // こちらも ARGB32 の場合 BGRA に並べ替える必要がある
        if (format == fcPixelFormat_RGBAu8) {
            copy_with_BGRA_RGBA_conversion((RGBA<uint8_t>*)wpixels, (RGBA<uint8_t>*)rpixels, int(bufsize / 4));
        }
        else {
            memcpy(wpixels, rpixels, bufsize);
        }
        surf_src->UnlockRect();

        hr = m_device->UpdateSurface(surf_src, nullptr, surf_dst, nullptr);
        if (SUCCEEDED(hr)) {
            ret = true;
        }
    }
    surf_dst->Release();

    return false;
}

#endif // fcSupportD3D9
