#include <UdpServer.hpp>
#include <Scope.hpp>
#include <WebRtcRelayUtils.hpp>
#include <Log.hpp>
#include <boost/foreach.hpp>

#include <srtp.h>

extern Scope gGlobalScope;

#define SRTCP_INDEX_LEN 4
#define SRTCP_MAX_TRAILER_LEN SRTP_MAX_TRAILER_LEN + SRTCP_INDEX_LEN

UdpServer::UdpServer(boost::asio::io_service& ioService,
                     const boost::asio::ip::address& listeningAddr):
    _ioService(ioService),
    _port(0),
    _socket(_ioService, boost::asio::ip::udp::v4()),
    _listeningAddr(listeningAddr)
{
    StunAgentUsageFlags flags = (StunAgentUsageFlags)(STUN_AGENT_USAGE_SHORT_TERM_CREDENTIALS
        | STUN_AGENT_USAGE_USE_FINGERPRINT
        | STUN_AGENT_USAGE_NO_INDICATION_AUTH);
    stun_agent_init(&_stunAgent, STUN_ALL_KNOWN_ATTRIBUTES, STUN_COMPATIBILITY_RFC5389,
        flags);

    err_status_t status = srtp_init();
    assert(status == err_status_ok);
}

UdpServer::~UdpServer()
{
    //srtp_dealloc(_srtpOutboundSession);
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
}

void UdpServer::stopAsync()
{
    _ioService.post(boost::bind(&UdpServer::stopInternal, this));
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

void UdpServer::addLink(const std::vector<sm_uint8_t>& iceUname, UserPtr user,
        MediaLinkType linkType, sm_uint32_t audioSsrc, sm_uint32_t videoSsrc,
        int downlinkUserId, sm_uint32_t downlinkUserPeerVideoSsrc)
{
    assert((linkType == MEDIA_LINK_TYPE_UPLINK) || (linkType == MEDIA_LINK_TYPE_DOWNLINK));
    LinkHelper lh = {user, linkType, downlinkUserId, downlinkUserPeerVideoSsrc};
    _iceUnames.insert(std::make_pair(iceUname, lh));

    _ssrcUsers.insert(std::make_pair(audioSsrc, lh));
    _ssrcUsers.insert(std::make_pair(videoSsrc, lh));
}

void UdpServer::removeLink(const std::vector<sm_uint8_t>& uname)
{
    UserToLinkMap::iterator it = _iceUnames.find(uname);
    if (it != _iceUnames.end())
    {
        LinkHelper& lh = it->second;
        if (lh.linkType == MEDIA_LINK_TYPE_UPLINK)
        {
            _ssrcUsers.erase(lh.user->_uplink.peerAudioSsrc);
            _ssrcUsers.erase(lh.user->_uplink.peerVideoSsrc);
        }
        if (lh.linkType == MEDIA_LINK_TYPE_DOWNLINK)
        {
            LinkInfo& li = lh.user->_downlinks[lh.senderUserId];
            _ssrcUsers.erase(li.peerAudioSsrc);
            _ssrcUsers.erase(li.peerVideoSsrc);
        }

        _iceUnames.erase(it);
    }
    
}

void UdpServer::addUser(UserPtr user)
{
    addLink(user->_uplink.iceCredentials->verifyingUname(),
        user, MEDIA_LINK_TYPE_UPLINK, user->_uplink.peerAudioSsrc,
        user->_uplink.peerVideoSsrc, 0, 0);
}

void UdpServer::removeUser(UserPtr user)
{
    removeLink(user->_uplink.iceCredentials->verifyingUname());

    // disposing all downlinks associated with this user
    BOOST_FOREACH(User::DownlinksMap::value_type& v,
        user->_downlinks)
    {
        removeLink(v.second.iceCredentials->verifyingUname());
    }
}

void UdpServer::requestFir(sm_uint32_t senderSsrc)
{
    SsrcToUserMap::iterator it = _ssrcUsers.find(senderSsrc);
    if (it == _ssrcUsers.end())
    {
        LOG_E("No uplink found for SSRC " << senderSsrc);
        assert(!"No uplink for SSRC");
        return;
    }

    UserPtr sender = it->second.user;
    assert(senderSsrc == sender->_uplink.peerVideoSsrc);
    std::vector<sm_uint8_t> buf;
    generateRtcpFir(buf, sender->_uplink.streamerVideoSsrc, senderSsrc, 0);

    int packetLen = buf.size();
    buf.resize(packetLen + SRTCP_MAX_TRAILER_LEN);
    err_status_t res = srtp_protect_rtcp(sender->_uplink.srtpStreamerSession, &buf[0], &packetLen);
    if (res != err_status_ok)
    {
        LOG_E("Failed to encode incoming RTCP packet: " << res);
        return;
    }

    _socket.send_to(boost::asio::buffer(&buf[0], packetLen), sender->_uplink.transportEndpoint.udpEndpoint());
    LOG_D("FIR sent from ssrc " << sender->_uplink.streamerVideoSsrc
        << " to ssrc " << senderSsrc << " on endpoint " << sender->_uplink.transportEndpoint.udpEndpoint());
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

    
    sm_uint8_t* data = (sm_uint8_t*)_recvBuffer.data();
    if (isStun(data, size))
        handleStunPacket(data, size);
    else
        handleMediaPacket(data, size);

    startReceive();
}

void UdpServer::handleMediaPacket(sm_uint8_t* data, size_t size)
{
    bool shouldBroadcast = true;
    sm_uint32_t ssrc = getSsrc(data, size);
    if (!ssrc)
        return;

    SsrcToUserMap::iterator it = _ssrcUsers.find(ssrc);
    if (it == _ssrcUsers.end())
    {
        LOG_W("Unknown SSRC " << ssrc << " from " << _remoteEndpoint
            << (isRtcp(data, size) ? "; RTCP": "; RTP") << "; payload: " << getRtcpType(data, size));
        return;
    }

    UserPtr user = it->second.user;

    // TODO: additional check for remote endpoint if it's registered

    // decode with original uplink SRTP context
    int packetLen = size;
    err_status_t res;
    srtp_t session;

    if (it->second.linkType == MEDIA_LINK_TYPE_DOWNLINK)
    {
        session = user->_downlinks[it->second.senderUserId].srtpPeerSession;
    }
    else
    {
        session = user->_uplink.srtpPeerSession;
    }

    bool rtcp = false;
    if (isRtcp(data, size))
    {
        rtcp = true;
        res = srtp_unprotect_rtcp(session, data, &packetLen);
    }
    else
    {
        res = srtp_unprotect(session, data, &packetLen);
    }

    // TODO: block RTCP RR propagation (?)
    // TODO: generate RTCP RR from incoming RTCP SR (?)

    if (res != err_status_ok)
    {
        LOG_E("Failed to decode " << (rtcp ? "RTCP": "RTP") << " incoming packet; code: " << res
            << "; SSRC: " << ssrc << "; key: " << base64Encode(&user->_uplink.peerKeySalt[0], user->_uplink.peerKeySalt.size()));
        return;
    }

    if (shouldBroadcast)
        broadcast(user, data, packetLen);
}


void UdpServer::broadcast(const UserPtr& uplinkUser, sm_uint8_t* data, size_t size)
{
    std::vector<std::pair<TransportEndpoint, srtp_t> > endpoints =
        gGlobalScope.getDownlinkEndpointsFor(uplinkUser->_userId);
    typedef std::pair<TransportEndpoint, srtp_t> EP;
    BOOST_FOREACH(EP& te, endpoints)
    {
        if (!te.first.isSet())
        {
            // a few packets get here, because ICE processing takes time to finish
            // after new downlink connection is added in signaling channel
            LOG_W("Zero UDP port to send to");
            continue;
        }

        err_status_t res;
        int packetLen = (int)size;
        // reencode with downlink context
        srtp_t session = te.second;
        if (isRtcp(data, size))
            res = srtp_protect_rtcp(session, data, &packetLen);
        else
            res = srtp_protect(session, data, &packetLen);

        if (res != err_status_ok)
        {
            LOG_E("Failed to encode incoming RTCP packet: " << res);
            return;
        }

        _socket.send_to(boost::asio::buffer(data, packetLen), te.first.udpEndpoint());
    }
}

bool UdpServer::handleRtcpPacket(const sm_uint8_t* data, size_t size)
{
    return false;
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
        lh.user->getIcePassword(lh.linkType, lh.senderUserId,
            password, passwordLen);
        // set mapped user info as side effect:
        _this->_iceUserRef = lh;
        return true;
    }

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
                // this should not happen by design,
                // our ICE-LITE agent is always controlled
                LOG_E("Fatal: role conflict");
                assert(!"ICE role conflict");
            }
            else if (res == STUN_USAGE_ICE_RETURN_SUCCESS)
            {
                bool useCandidate = stun_usage_ice_conncheck_use_candidate(&request);
                if (useCandidate)
                {
                    // we've just concluded ICE-LITE processing according to RFC 5445 8.2.1
                    TransportEndpoint te(_remoteEndpoint);
                    bool result = _iceUserRef.user->updateIceEndpoint(_iceUserRef.linkType,
                        _iceUserRef.senderUserId, te);

                    if (result && _iceUserRef.linkType == MEDIA_LINK_TYPE_DOWNLINK)
                    {
                        LOG_D("New donlink connection established. Requesting FIR for all other participants");
                        requestFir(_iceUserRef.senderVideoSsrc);
                    }
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
