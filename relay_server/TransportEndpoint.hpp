#ifndef ___TransportEndpoint_hpp__
#define ___TransportEndpoint_hpp__

#include <boost/asio.hpp>

class TransportEndpoint
{
public:
    TransportEndpoint() {}

    explicit TransportEndpoint(const boost::asio::ip::udp::endpoint& udpEndpoint)
    {
        _udpEndpoint = udpEndpoint;
    }

    bool isSet() const
    {
        return _udpEndpoint.port() != 0;
    }

    boost::asio::ip::udp::endpoint udpEndpoint() const
    {
        return _udpEndpoint;
    }

private:
    boost::asio::ip::udp::endpoint _udpEndpoint;
};


#endif