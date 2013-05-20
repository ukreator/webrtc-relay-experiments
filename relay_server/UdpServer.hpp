#ifndef ___UdpServer_h__
#define ___UdpServer_h__

#include <stun/usages/ice.h>

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/thread.hpp>
#include <boost/cstdint.hpp>

#include <set>
#include <map>
#include <string>

typedef boost::uint16_t sm_uint16_t;
typedef boost::uint8_t sm_uint8_t;
typedef boost::uint64_t sm_uint64_t;
typedef boost::uint32_t sm_uint32_t;

class UdpServer: boost::noncopyable
{
public:
    UdpServer(const boost::asio::ip::address& listeningAddr);

    ~UdpServer();

    void start(sm_uint16_t port);

    void stop();

    void sendPacket(const sm_uint8_t*, size_t, const boost::asio::ip::udp::endpoint& targetEndpoint);

    void addRecognizedIceUser(const std::string& user, const std::string& pwd);

private:

    void handleStunPacket(const sm_uint8_t* data, size_t size);

    void startReceive();

    void handleReceive(const boost::system::error_code& error,
        std::size_t size);

    void stopInternal();

    void run();

    static bool validateStunCredentials(StunAgent *agent,
        StunMessage *message, uint8_t *username, uint16_t usernameLen,
        uint8_t **password, size_t *passwordLen, void *userData);


    boost::asio::io_service _ioService;
	sm_uint16_t _port;
    boost::asio::ip::udp::socket _socket;
    boost::asio::ip::udp::endpoint _remoteEndpoint;
    boost::array<char, 4000> _recvBuffer;
    boost::asio::ip::address _listeningAddr;

    boost::thread _ioServiceThread;

    StunAgent _stunAgent;
    StunAgent _indicationStunAgent;
    typedef std::map<std::vector<sm_uint8_t>, std::vector<sm_uint8_t> > UserPassMap;
    UserPassMap _recognizedIceUsers;
};


#endif
