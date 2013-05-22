#ifndef ___UdpServer_hpp__
#define ___UdpServer_hpp__

#include <IntTypes.hpp>
#include <User.hpp>

#include <stun/usages/ice.h>

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/thread.hpp>
#include <boost/cstdint.hpp>

#include <set>
#include <map>
#include <string>


class UdpServer: boost::noncopyable
{
public:
    UdpServer(const boost::asio::ip::address& listeningAddr);

    ~UdpServer();

    void start(sm_uint16_t port);

    void stop();

    void sendPacket(const sm_uint8_t*, size_t, const boost::asio::ip::udp::endpoint& targetEndpoint);

    void addRecognizedIceUser(const std::vector<sm_uint8_t>& user, const std::vector<sm_uint8_t>& pwd);

    void removeRecognizedIceUser(const std::vector<sm_uint8_t>& user);

    void addUser(UserPtr user);

    void removeUser(UserPtr user);

private:

    void handleStunPacket(const sm_uint8_t* data, size_t size);

    bool handleRtcpPacket(const sm_uint8_t* data, size_t size);

    void broadcast(const sm_uint8_t* data, size_t size);

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
    typedef std::map<std::vector<sm_uint8_t>, std::vector<sm_uint8_t> > UserPassMap;
    UserPassMap _recognizedIceUsers;

    std::map<sm_uint32_t, UserPtr> _ssrcUsers;
};


#endif
