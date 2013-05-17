#include <UdpServer.hpp>
#include <boost/foreach.hpp>


#define LOG_D(x) std::cout << x << std::endl
#define LOG_E(x) LOG_D(x)
#define LOG_I(x) LOG_D(x)
#define LOG_W(x) LOG_D(x)


UdpServer::UdpServer(const boost::asio::ip::address& listeningAddr):
    _port(0),
    _socket(_ioService, boost::asio::ip::udp::v4()),
    _listeningAddr(listeningAddr)
{
    StunAgentUsageFlags flags = (StunAgentUsageFlags)(STUN_AGENT_USAGE_SHORT_TERM_CREDENTIALS
        | STUN_AGENT_USAGE_USE_FINGERPRINT
        | STUN_AGENT_USAGE_NO_INDICATION_AUTH);
    stun_agent_init(&_stunAgent, STUN_ALL_KNOWN_ATTRIBUTES, STUN_COMPATIBILITY_RFC5389,
        flags);
}

UdpServer::~UdpServer()
{
}

void UdpServer::start(sm_uint16_t port)
{
    boost::system::error_code ec;

    using namespace boost::asio::ip;
    udp::endpoint ep(_listeningAddr, port);
    _socket.bind(ep);

    startReceive();

    _port = port;
    LOG_I("UDP server started listening on " << ep);

    _ioServiceThread = boost::thread(boost::bind(&UdpServer::run, this));
}

void UdpServer::stop()
{
    _ioService.post(boost::bind(&UdpServer::stopInternal, this));
    _ioServiceThread.join();
}

void UdpServer::stopInternal()
{
    LOG_D("Closing UdpServer socket for port " << _port);
    boost::system::error_code ecIgnore;
    _socket.close(ecIgnore);
    if (ecIgnore)
        LOG_E("Failed to close socket: " << ecIgnore.value());
}

void UdpServer::sendPacket(const sm_uint8_t* p, size_t size,
        const boost::asio::ip::udp::endpoint& targetEndpoint)
{
    boost::system::error_code ec;
    _socket.send_to(boost::asio::buffer(p, size),
                targetEndpoint, 0, ec);
    if (ec)
    {
        LOG_E("Failed to send data: " << ec.value() << " to " << targetEndpoint);
    }
}


void UdpServer::startReceive()
{
    _socket.async_receive_from(
        boost::asio::buffer(_recvBuffer), _remoteEndpoint,
        boost::bind(&UdpServer::handleReceive, this,
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
}

namespace
{
bool validateStunCredentials(StunAgent *agent,
    StunMessage *message, uint8_t *username, uint16_t username_len,
    uint8_t **password, size_t *passwordLen, void *userData)
{
    UdpServer* _this = (UdpServer*)userData;
    // - find User by username, if not exist => return false
    // - write password to out params (TODO: do we need to conver to bytes?)

    return false;
}
}

void UdpServer::handleReceive(const boost::system::error_code& error,
    std::size_t size)
{
    if (error)
    {
        if (error == boost::asio::error::operation_aborted)
        {
            LOG_D("Receiving operation aborted by our application.");
            return;
        }
        else if (error == boost::asio::error::connection_reset)
        {
            // could happen if we get ICMP "failed to deliver" messages
            LOG_E("Could not deliver some packets for endpoint "
                  << _remoteEndpoint << ": " << error.value());
        }
        else if (error == boost::asio::error::connection_refused)
        {
            LOG_E("Remote endpoint " << _remoteEndpoint
                  << " cannot be reached: " << error.value());
        }
        else
        {
            LOG_E("Got unknown error while receiving data. "
                  << error.value());
            return;
        }
    }

    if (size == 0)
    {
        startReceive();
        return;
    }

    // todo: demultiplex STUN and RTP/RTCP packets


    //LOG_D("Got UDP packet of size " << size);
    StunMessage stunMessage;
    StunValidationStatus status = stun_agent_validate(
        &_stunAgent, &stunMessage, (const uint8_t*)_recvBuffer.data(), size,
        validateStunCredentials, this);

    LOG_D("STUN request validated with result: " << status);
    // todo: error handling, generating response back if it's connectivity checks

    startReceive();
}

void UdpServer::run()
{
    _ioService.run();
}
