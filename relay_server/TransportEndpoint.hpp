#ifndef ___TransportEndpoint_hpp__
#define ___TransportEndpoint_hpp__

#include <boost/asio.hpp>

class TransportEndpoint
{
public:

    boost::asio::ip::udp::endpoint _udpEndpoint;
};


#endif