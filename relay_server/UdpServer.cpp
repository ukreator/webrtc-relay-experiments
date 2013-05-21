#include <UdpServer.hpp>
#include <boost/foreach.hpp>


#define LOG_D(x) std::cout << "[DEBUG] " << x << std::endl
#define LOG_E(x) std::cout << "[ERROR] " << x << std::endl
#define LOG_I(x) std::cout << "[ INFO] " << x << std::endl
#define LOG_W(x) std::cout << "[ WARN] " << x << std::endl

namespace
{
bool isStun(const sm_uint8_t* data, size_t len)
{
    if (len < 8)
        return false;
    // first 2 bits should be 0
    if ((data[0] & 0xC0) != 0)
        return false;
    // magic cookie, 4 bytes starting from 4th byte
    const sm_uint8_t* p = data + STUN_MESSAGE_TRANS_ID_POS;
    sm_uint32_t cookie = (p[0] << 24) |
        (p[1] << 16) | (p[2] << 8) | p[3];
    return cookie == STUN_MAGIC_COOKIE;
}
}


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

void UdpServer::addRecognizedIceUser(const std::string& user, const std::string& pwd)
{
    std::vector<sm_uint8_t> uname(user.begin(), user.end());
    std::vector<sm_uint8_t> passwd(pwd.begin(), pwd.end());
    _recognizedIceUsers.insert(std::make_pair(uname, passwd));
}

void UdpServer::removeRecognizedIceUser(const std::string& user)
{
    _recognizedIceUsers.erase(std::vector<sm_uint8_t>(user.begin(), user.end()));
}

bool UdpServer::validateStunCredentials(StunAgent *agent,
    StunMessage *message, uint8_t *username, uint16_t usernameLen,
    uint8_t **password, size_t *passwordLen, void *userData)
{
    UdpServer* _this = (UdpServer*)userData;

    std::vector<sm_uint8_t> uname(username, username + usernameLen);
    UserPassMap::iterator it = _this->_recognizedIceUsers.find(uname);
    if (it != _this->_recognizedIceUsers.end())
    {
        *password = &(it->second[0]);
        *passwordLen = it->second.size();
        return true;
    }

    return false;
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

    const sm_uint8_t* data = (const sm_uint8_t*)_recvBuffer.data();
    if (isStun(data, size))
    {
        handleStunPacket(data, size);
    }
    else
    {
        // todo: RTP handling
    }

    startReceive();
}

void UdpServer::handleStunPacket(const sm_uint8_t* data, size_t size)
{
    StunMessage request;
    StunMessage msg;
    std::vector<sm_uint8_t> rbuf(1500);
    StunValidationStatus valid = stun_agent_validate(
        &_stunAgent, &request, data, size,
        validateStunCredentials, this);

    if (valid == STUN_VALIDATION_SUCCESS)
    {
        StunClass msgClass = stun_message_get_class(&request);
        if (msgClass == STUN_REQUEST)
        {
            size_t rbufLen = rbuf.size();
            bool control = false;
            sm_uint64_t tieBreaker = 0; // not important for ice-lite
            size_t res = stun_usage_ice_conncheck_create_reply(&_stunAgent, &request,
                &msg, &rbuf[0], &rbufLen, _remoteEndpoint.data(), sizeof (sockaddr),
                &control, tieBreaker,
                STUN_USAGE_ICE_COMPATIBILITY_RFC5245);

            if (res == STUN_USAGE_ICE_RETURN_ROLE_CONFLICT)
            {
                LOG_E("Fatal: error conflict");
            }
            else if (res == STUN_USAGE_ICE_RETURN_SUCCESS)
            {
                bool useCandidate = stun_usage_ice_conncheck_use_candidate(&request);
                LOG_D("Should be used as nominated: " << useCandidate);
                _socket.send_to(boost::asio::buffer(&rbuf[0], rbufLen), _remoteEndpoint);
            }
            else
            {
                LOG_E("Failed to create ICE answer: " << res);
            }
        }
        else if (msgClass == STUN_INDICATION)
        {
            LOG_D("Got indication request");
        }
        else
        {
            LOG_E("STUN response received");
        }
    }
    else if (valid == STUN_VALIDATION_INCOMPLETE_STUN ||
             valid == STUN_VALIDATION_BAD_REQUEST)
    {
        LOG_E("Invalid STUN packet");
    }
    else if (valid == STUN_VALIDATION_BAD_REQUEST)
    {
        // not enough attributes set, wrong CRC
        LOG_E("Bad request");
        // todo: check if it's binding indication (via additional StunAgent instance) and response if necessary
    }
    else if (valid == STUN_VALIDATION_UNAUTHORIZED_BAD_REQUEST)
    {
        size_t rbufLen;
        if (stun_agent_init_error(&_stunAgent, &msg, &rbuf[0], rbuf.size(),
            &request, STUN_ERROR_BAD_REQUEST))
        {
            rbufLen = stun_agent_finish_message(&_stunAgent, &msg, NULL, 0);
            if (rbufLen != 0)
                _socket.send_to(boost::asio::buffer(&rbuf[0], rbufLen), _remoteEndpoint);
        }
    }
    else if (valid == STUN_VALIDATION_UNKNOWN_REQUEST_ATTRIBUTE)
    {
        LOG_E("Unknown request attribute");
        size_t rbufLen = stun_agent_build_unknown_attributes_error(&_stunAgent,
            &msg, &rbuf[0], rbuf.size(), &request);
        if (rbufLen != 0)
            _socket.send_to(boost::asio::buffer(&rbuf[0], rbufLen), _remoteEndpoint);
    }
    else if (valid == STUN_VALIDATION_UNAUTHORIZED)
    {
        LOG_E("Authorization check failed");
        size_t rbufLen;
        if (stun_agent_init_error(&_stunAgent, &msg, &rbuf[0], rbuf.size(),
            &request, STUN_ERROR_UNAUTHORIZED))
        {
            rbufLen = stun_agent_finish_message(&_stunAgent, &msg, NULL, 0);
            if (rbufLen != 0)
                _socket.send_to(boost::asio::buffer(&rbuf[0], rbufLen), _remoteEndpoint);
        }
    }
    else
    {
        LOG_E("Validation error: " << valid);
    }
}

void UdpServer::run()
{
    _ioService.run();
}
