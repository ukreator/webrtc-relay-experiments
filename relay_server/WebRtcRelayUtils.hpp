#ifndef ___WebRtcRelayUtils_hpp__
#define ___WebRtcRelayUtils_hpp__

#include <IntTypes.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <vector>

/*
 * Generates a stream of octets containing only characters
 * with ASCII codecs of 0x41-5A (A-Z), 0x61-7A (a-z), 
 * 0x30-39 (0-9), 0x2b (+) and 0x2f (/). This matches 
 * the definition of 'ice-char' in ICE Ispecification,
 * section 15.1 (ID-16).
 * NOTE: modeled after nice_rng_generate_bytes_print() from libnice.
 */
void generatePrintableBytes(size_t size, boost::mt19937& gen, std::vector<sm_uint8_t>* outBuf);

/**
 * Returns true if buffer @data of size @len has signatures of STUN packet (RFC 5389 only)
 */
bool isStun(const sm_uint8_t* data, size_t len);

void hostToNetwork(sm_uint32_t val, sm_uint8_t* out);

sm_uint32_t networkToHost(const sm_uint8_t* val);

sm_uint32_t getSsrc(const sm_uint8_t* data, size_t len);

#endif
