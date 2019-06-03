#pragma once

#include "fcInternal.h"

#ifdef fcSupportWebM
#include "Foundation/fcFoundation.h"
#include "GraphicsDevice/fcGraphicsDevice.h"


struct fcWebMPacketInfo
{
    uint32_t size;
    double timestamp;
    uint32_t keyframe : 1;
};

struct fcWebMFrameData
{
    using Packets = RawVector<fcWebMPacketInfo>;
    Buffer data;
    Packets packets;

    void clear()
    {
        data.clear();
        packets.clear();
    }

    // Body: [](const char *data, const fcWebMPacketInfo& pinfo) {}
    template<class Body>
    void eachPackets(const Body& body) const
    {
        int pos = 0;
        for (auto& p : packets) {
            body(&data[pos], p);
            pos += p.size;
        }
    }
};


class fcIWebMEncoderInfo
{
public:
    virtual ~fcIWebMEncoderInfo() {}
    virtual const char* getMatroskaCodecID() const = 0;
    virtual const Buffer& getCodecPrivate() const = 0;
};

class fcIWebMVideoEncoder : public fcIWebMEncoderInfo
{
public:
    virtual ~fcIWebMVideoEncoder() {}
    virtual bool encode(fcWebMFrameData& dst, const void *image, fcPixelFormat fmt, fcTime timestamp, bool force_keyframe = false) = 0;
    virtual bool flush(fcWebMFrameData& dst) = 0;
};

class fcIWebMAudioEncoder : public fcIWebMEncoderInfo
{
public:
    virtual ~fcIWebMAudioEncoder() {}
    virtual bool encode(fcWebMFrameData& dst, const float *samples, size_t num_samples) = 0;
    virtual bool flush(fcWebMFrameData& dst) = 0;
};

#endif // fcSupportWebM
