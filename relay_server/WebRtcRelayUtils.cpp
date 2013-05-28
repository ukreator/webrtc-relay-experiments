#include <WebRtcRelayUtils.hpp>

#include <stun/constants.h>

#include <boost/random/uniform_int.hpp>
#include <boost/random/variate_generator.hpp>

namespace
{

const int gSsrcRtpOffset = 8;
const int gSsrcRtcpOffset = 4;

}

void generatePrintableBytes(size_t size, boost::mt19937& gen, std::vector<sm_uint8_t>* outBuf)
{
    outBuf->resize(size);
    const char chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789"
        "+/";

    boost::uniform_int<> dist(0, sizeof(chars) - 2);
    boost::variate_generator<boost::mt19937&, boost::uniform_int<> > rndval(
            gen, dist);
    
    for (size_t i = 0; i < size; ++i)
      (*outBuf)[i] = chars[rndval()];
}

bool isStun(const sm_uint8_t* data, size_t len)
{
    if (len < 8)
        return false;
    // first 2 bits should be 0
    if ((data[0] & 0xC0) != 0)
        return false;
    // magic cookie, 4 bytes starting from 4th byte
    const sm_uint8_t* p = data + STUN_MESSAGE_TRANS_ID_POS;
    sm_uint32_t cookie = (p[0] << 24) |
        (p[1] << 16) | (p[2] << 8) | p[3];
    return cookie == STUN_MAGIC_COOKIE;
}

void hostToNetwork(sm_uint32_t val, sm_uint8_t* out)
{
    out[0] = (val >> 24) & 0xff;
    out[1] = (val >> 16) & 0xff;
    out[2] = (val >> 8) & 0xff;
    out[3] = val & 0xff;
}

sm_uint32_t networkToHost(const sm_uint8_t* val)
{
    return (val[0] << 24) | (val[1] << 16) | (val[2] << 8) | val[3];
}

sm_uint32_t getSsrc(const sm_uint8_t* data, size_t len)
{
    sm_uint32_t ssrc = 0;
    if (len >= 12)
    {    
        int offset = isRtcp(data, len) ? gSsrcRtcpOffset: gSsrcRtpOffset;
        ssrc = networkToHost(data + offset);
    }
    return ssrc;
}

bool isRtcp(const sm_uint8_t* data, size_t len)
{
    if (len < 8)
        return false;
    if ((data[0] & 0xC0) != 0x80)
        return false;
    return data[1] <= RTCP_PSFB && data[1] >= RTCP_SR;
}

RtcpType getRtcpType(const sm_uint8_t* data, size_t len)
{
    assert(isRtcp(data, len));
    return (RtcpType)data[1];
}