#include <WebRtcRelayUtils.hpp>

#include <stun/constants.h>

#include <random>
#include <assert.h>

//#include <boost/random/uniform_int.hpp>
//#include <boost/random/variate_generator.hpp>

namespace
{

const int gSsrcRtpOffset = 8;
const int gSsrcRtcpOffset = 4;

const int gSrtpMasterKeyLen = 16;
const int gSrtpMasterSaltLen = 14;

}

void generatePrintableBytes(size_t size, std::mt19937& gen, std::vector<sm_uint8_t>* outBuf)
{
    outBuf->resize(size);
    const char chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789"
        "+/";

    std::uniform_int_distribution<int> dist(0, sizeof(chars) - 2);
    
    for (size_t i = 0; i < size; ++i)
      (*outBuf)[i] = chars[dist(gen)];
}

void generateRandomBinaryVector(size_t size, std::mt19937& gen, std::vector<sm_uint8_t>* outBuf)
{
    outBuf->resize(size);
    std::uniform_int_distribution<int> dist(0, 255);
    
    for (size_t i = 0; i < size; ++i)
      (*outBuf)[i] = dist(gen);
}

std::string generateKeySalt(std::mt19937& gen)
{
    std::vector<sm_uint8_t> keySalt;
    generateRandomBinaryVector(gSrtpMasterKeyLen + gSrtpMasterSaltLen, gen, &keySalt);
    return base64Encode(&keySalt[0], keySalt.size());
}

std::vector<sm_uint8_t> defaultSizeKeySalt(std::mt19937& gen)
{
    std::vector<sm_uint8_t> keySalt;
    generateRandomBinaryVector(gSrtpMasterKeyLen + gSrtpMasterSaltLen, gen, &keySalt);
    return keySalt;
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

void generateRtcpFir(std::vector<sm_uint8_t>& result, sm_uint32_t fromSsrc, sm_uint32_t toSsrc,
                     sm_uint8_t seqNumber)
{
    result.resize(20);
    result[0] = 0x84;
    result[1] = RTCP_PSFB;

    // length is 4 in network byte order
    result[2] = 0;
    result[3] = 4;

    // this packet originator SSRC
    hostToNetwork(fromSsrc, &result[4]);
    // zero media source
    hostToNetwork(0, &result[8]);

    // video sender SSRC
    hostToNetwork(toSsrc, &result[12]);
    // seq number
    result[16] = seqNumber;
    // zeroing rest 3 bytes
    result[17] = result[18] = result[19] = 0;
}

std::string base64Encode(const std::vector<sm_uint8_t>& data)
{
    return base64Encode(&data[0], data.size());
}

/* 
   Base64 encode/decode functions.

   Copyright (C) 2004-2008 René Nyffenegger

   This source code is provided 'as-is', without any express or implied
   warranty. In no event will the author be held liable for any damages
   arising from the use of this software.

   Permission is granted to anyone to use this software for any purpose,
   including commercial applications, and to alter it and redistribute it
   freely, subject to the following restrictions:

   1. The origin of this source code must not be misrepresented; you must not
      claim that you wrote the original source code. If you use this source code
      in a product, an acknowledgment in the product documentation would be
      appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
      misrepresented as being the original source code.

   3. This notice may not be removed or altered from any source distribution.

   René Nyffenegger rene.nyffenegger@adp-gmbh.ch

*/

static const std::string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";


static inline bool is_base64(unsigned char c) {
  return (isalnum(c) || (c == '+') || (c == '/'));
}

std::string base64Encode(unsigned char const* bytes_to_encode, unsigned int in_len) {
  std::string ret;
  int i = 0;
  int j = 0;
  unsigned char char_array_3[3];
  unsigned char char_array_4[4];

  while (in_len--) {
    char_array_3[i++] = *(bytes_to_encode++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;

      for(i = 0; (i <4) ; i++)
        ret += base64_chars[char_array_4[i]];
      i = 0;
    }
  }

  if (i)
  {
    for(j = i; j < 3; j++)
      char_array_3[j] = '\0';

    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
    char_array_4[3] = char_array_3[2] & 0x3f;

    for (j = 0; (j < i + 1); j++)
      ret += base64_chars[char_array_4[j]];

    while((i++ < 3))
      ret += '=';

  }

  return ret;

}

std::vector<sm_uint8_t> base64Decode(std::string const& encoded_string) {
  int in_len = encoded_string.size();
  int i = 0;
  int j = 0;
  int in_ = 0;
  unsigned char char_array_4[4], char_array_3[3];
  std::vector<sm_uint8_t> ret;

  while (in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
    char_array_4[i++] = encoded_string[in_]; in_++;
    if (i ==4) {
      for (i = 0; i <4; i++)
        char_array_4[i] = base64_chars.find(char_array_4[i]);

      char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

      for (i = 0; (i < 3); i++)
        ret.push_back(char_array_3[i]);
      i = 0;
    }
  }

  if (i) {
    for (j = i; j <4; j++)
      char_array_4[j] = 0;

    for (j = 0; j <4; j++)
      char_array_4[j] = base64_chars.find(char_array_4[j]);

    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

    for (j = 0; (j < i - 1); j++) ret.push_back(char_array_3[j]);
  }

  return ret;
}
