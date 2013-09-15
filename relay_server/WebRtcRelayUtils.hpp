#ifndef ___WebRtcRelayUtils_hpp__
#define ___WebRtcRelayUtils_hpp__

#include <IntTypes.hpp>
#include <random>
#include <vector>

enum RtcpType
{
    RTCP_SR = 200,
    RTCP_RR,
    RTCP_SDES,
    RTCP_BYE,
    RTCP_APP,
    RTCP_RTPFB,
    RTCP_PSFB
};

/*
 * Generates a stream of octets containing only characters
 * with ASCII codecs of 0x41-5A (A-Z), 0x61-7A (a-z), 
 * 0x30-39 (0-9), 0x2b (+) and 0x2f (/). This matches 
 * the definition of 'ice-char' in ICE Ispecification,
 * section 15.1 (ID-16).
 * NOTE: modeled after nice_rng_generate_bytes_print() from libnice.
 */
void generatePrintableBytes(size_t size, std::mt19937& gen, std::vector<sm_uint8_t>* outBuf);

void generateRandomBinaryVector(size_t size, std::mt19937& gen, std::vector<sm_uint8_t>* outBuf);

std::string generateKeySalt(std::mt19937& gen);

std::vector<sm_uint8_t> defaultSizeKeySalt(std::mt19937& gen);

/**
 * Returns true if buffer @data of size @len has signatures of STUN packet (RFC 5389 only)
 */
bool isStun(const sm_uint8_t* data, size_t len);

void hostToNetwork(sm_uint32_t val, sm_uint8_t* out);

sm_uint32_t networkToHost(const sm_uint8_t* val);

sm_uint32_t getSsrc(const sm_uint8_t* data, size_t len);

bool isRtcp(const sm_uint8_t* data, size_t len);

RtcpType getRtcpType(const sm_uint8_t* data, size_t len);

std::string base64Encode(const std::vector<sm_uint8_t>& data);

std::string base64Encode(unsigned char const* bytes_to_encode, unsigned int in_len);

std::vector<sm_uint8_t> base64Decode(std::string const& encoded_string);

void generateRtcpFir(std::vector<sm_uint8_t>& result, sm_uint32_t fromSsrc, sm_uint32_t toSsrc,
                     sm_uint8_t seqNumber);

#endif
