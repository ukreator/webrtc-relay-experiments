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

#endif
