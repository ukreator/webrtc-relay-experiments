#include <UdpServer.hpp>
#include <Scope.hpp>
#include <WebRtcRelayUtils.hpp>
#include <Log.hpp>
#include <boost/foreach.hpp>


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

void UdpServer::addIceUser(const std::vector<sm_uint8_t>& iceUname, UserPtr user,
        MediaLinkType linkType, int downlinkUserId)
{
    assert((linkType == MEDIA_LINK_TYPE_UPLINK) || (linkType == MEDIA_LINK_TYPE_DOWNLINK));
    LinkHelper lh = {user, linkType, downlinkUserId};
    _iceUnames.insert(std::make_pair(iceUname, lh));
}

void UdpServer::removeIceUser(const std::vector<sm_uint8_t>& user)
{
    _iceUnames.erase(user);
}

void UdpServer::addUser(UserPtr user)
{
    _ssrcUsers.insert(std::make_pair(user->_audioSsrc, user));
    _ssrcUsers.insert(std::make_pair(user->_videoSsrc, user));

    addIceUser(user->_uplink.iceCredentials->verifyingUname(),
        user, MEDIA_LINK_TYPE_UPLINK, 0);
}

void UdpServer::removeUser(UserPtr user)
{
    _ssrcUsers.erase(user->_audioSsrc);
    _ssrcUsers.erase(user->_videoSsrc);
    removeIceUser(user->_uplink.iceCredentials->verifyingUname());

    // removing downlink ICE credentials:
    BOOST_FOREACH(User::DownlinksMap::value_type& v,
        user->_downlinks)
    {
        removeIceUser(v.second.iceCredentials->verifyingUname());
    }
}

bool UdpServer::validateStunCredentials(StunAgent *agent,
    StunMessage *message, uint8_t *username, uint16_t usernameLen,
    uint8_t **password, size_t *passwordLen, void *userData)
{
    UdpServer* _this = (UdpServer*)userData;

    std::vector<sm_uint8_t> uname(username, username + usernameLen);
    UserToLinkMap::iterator it = _this->_iceUnames.find(uname);
    if (it != _this->_iceUnames.end())
    {
        LinkHelper& lh = it->second;
        lh.user->getIcePassword(lh.linkType, lh.downlinkUserId,
            password, passwordLen);
        // set mapped user info as side effect:
        _this->_iceUserRef = lh;
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
        handleStunPacket(data, size);
    else
        handleMediaPacket(data, size);

    startReceive();
}

void UdpServer::handleMediaPacket(const sm_uint8_t* data, size_t size)
{
    bool shouldBroadcast = true;
    //if (isRtcp(data, size))
    //{
    //    broadcast = handleRtcpPacket(data, size);
    //}
    //else
    //{
    //    shouldBroadcast = true;
    //}

    sm_uint32_t ssrc = getSsrc(data, size);
    if (!ssrc)
        return;

    SsrcToUserMap::iterator it = _ssrcUsers.find(ssrc);
    if (it == _ssrcUsers.end())
    {
        LOG_W("Unknown SSRC " << ssrc);
        return;
    }

    UserPtr user = it->second;

    // TODO: check incoming SRTP/SRTCP packet authentication
    // silently drop packet if not authenticated

    if (shouldBroadcast)
        broadcast(user, data, size);

    // TODO: think how to handle ROC incremented before new user connected
    // and received media stream. Is it possible to extract ROC from incoming RTP,
    // save it in User and distribute along with new downlink connection establishment event
}


extern Scope gGlobalScope;
void UdpServer::broadcast(const UserPtr& uplinkUser, const sm_uint8_t* data, size_t size)
{
    std::vector<TransportEndpoint> endpoints = gGlobalScope.getDownlinkEndpointsFor(uplinkUser->_userId);
    BOOST_FOREACH(TransportEndpoint& te, endpoints)
    {
        _socket.send_to(boost::asio::buffer(data, size), te._udpEndpoint);
    }
}

bool UdpServer::handleRtcpPacket(const sm_uint8_t* data, size_t size)
{
    return false;
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
                // this should not happen by design:
                // our ICE-LITE agent is always controlled
                LOG_E("Fatal: role conflict");
                assert(!"ICE role conflict");
            }
            else if (res == STUN_USAGE_ICE_RETURN_SUCCESS)
            {
                bool useCandidate = stun_usage_ice_conncheck_use_candidate(&request);
                if (useCandidate)
                {
                    LOG_D("ICE concluded with endpoint " << _remoteEndpoint);
                    // we've just conclude ICE processing according to RFC 5445 8.2.1
                    TransportEndpoint te;
                    te._udpEndpoint = _remoteEndpoint;
                    _iceUserRef.user->updateIceEndpoint(_iceUserRef.linkType,
                        _iceUserRef.downlinkUserId, te);
                }
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
