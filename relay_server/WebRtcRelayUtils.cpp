#include <WebRtcRelayUtils.hpp>

#include <boost/random/uniform_int.hpp>
#include <boost/random/variate_generator.hpp>

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
