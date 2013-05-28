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
    UdpServer(boost::asio::io_service& ioService, const boost::asio::ip::address& listeningAddr);

    ~UdpServer();

    void start(sm_uint16_t port);

    void stop();

    void sendPacket(const sm_uint8_t*, size_t, const boost::asio::ip::udp::endpoint& targetEndpoint);

    void addLink(const std::vector<sm_uint8_t>& iceUname, UserPtr user,
        MediaLinkType linkType, int downlinkUserId, sm_uint32_t audioSsrc, sm_uint32_t videoSsrc);

    void removeLink(const std::vector<sm_uint8_t>& iceUname);

    void addUser(UserPtr user);

    void removeUser(UserPtr user);

private:

    void handleStunPacket(const sm_uint8_t* data, size_t size);

    bool handleRtcpPacket(const sm_uint8_t* data, size_t size);

    void handleMediaPacket(const sm_uint8_t* data, size_t size);

    void broadcast(const UserPtr& user, const sm_uint8_t* data, size_t size);

    void startReceive();

    void handleReceive(const boost::system::error_code& error,
        std::size_t size);

    void stopInternal();

    void run();

    static bool validateStunCredentials(StunAgent *agent,
        StunMessage *message, uint8_t *username, uint16_t usernameLen,
        uint8_t **password, size_t *passwordLen, void *userData);

    boost::asio::io_service& _ioService;

	sm_uint16_t _port;
    boost::asio::ip::udp::socket _socket;
    boost::asio::ip::udp::endpoint _remoteEndpoint;
    boost::array<char, 4000> _recvBuffer;
    boost::asio::ip::address _listeningAddr;

    // STUN context to answer to connectivity checks
    StunAgent _stunAgent;

    struct LinkHelper
    {
        UserPtr user;
        MediaLinkType linkType;
        int downlinkUserId;
    };
    typedef std::map<std::vector<sm_uint8_t>, LinkHelper> UserToLinkMap;
    UserToLinkMap _iceUnames;

    typedef std::map<sm_uint32_t, UserPtr> SsrcToUserMap;
    SsrcToUserMap _ssrcUsers;

    // this variable is set in validateStunCredentials
    LinkHelper _iceUserRef;
};


#endif
