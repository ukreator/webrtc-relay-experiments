#include <UdpServer.hpp>
#include <User.hpp>
#include <IceCredentials.hpp>
#include <SrtpSession.hpp>
#include <Scope.hpp>
#include <Log.hpp>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <json/json.h>
#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <iostream>
#include <set>

typedef websocketpp::server<websocketpp::config::asio> server;

using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

namespace
{
const std::string gServerIpAddr = "192.168.1.33";
const sm_uint16_t gServerPort = 7000;
const sm_uint16_t gSignalingPort = 10000;
const std::string gServerPortStr = "7000";
const std::string gServerCandidate = "0 1 UDP 2113667327 " + gServerIpAddr
    + " " + gServerPortStr + " typ host";
}


Scope gGlobalScope("1");


class BroadcastServer
{
public:
    explicit BroadcastServer(boost::asio::io_service& ioService): _ssrcCounter(0), _udpServer(0),
        _randomGenerator(sm_uint32_t(std::time(0)))
    {
        //_server.set_access_channels(websocketpp::log::alevel::all);
        _server.clear_access_channels(websocketpp::log::alevel::all);

        _server.init_asio(&ioService);
        _server.set_open_handler(bind(&BroadcastServer::onOpen, this, ::_1));
        _server.set_close_handler(bind(&BroadcastServer::onClose, this, ::_1));
        _server.set_message_handler(bind(&BroadcastServer::onMessage, this, ::_1,::_2));
    }

    void setUdpServer(UdpServer* udpServer)
    {
        _udpServer = udpServer;
    }
    
    void onOpen(connection_hdl hdl)
    {
        _connections.insert(hdl);
        LOG_D("New connection opened");
    }
    
    void onClose(connection_hdl hdl)
    {
        _connections.erase(hdl);
        
        UserPtr user = getUserByConnection(hdl);
        if (user)
        {
            LOG_D("Cleaning ICE and SSRC mappings");
            _udpServer->removeUser(user);
        }
        _signalingUsers.erase(hdl);
        
        LOG_D("Connection closed");
    }

    void reportConnectedUser(connection_hdl hdl, UserPtr user)
    {
        Json::Value uevent;
        Json::Value data;
        uevent["type"] = "userEvent";
        data["eventType"] = "newUser";
        data["userId"] = user->_userId;

        uevent["data"] = data;
        sendJson(hdl, uevent);
    }
    
    void handleAuthRequest(connection_hdl hdl, const Json::Value& params)
    {
        Json::Value result;
        int userId = params["userId"].asInt();
        std::string scopeId = params["scopeId"].asString();

        IceCredentialsPtr iceCreds = boost::make_shared<IceCredentials>(boost::ref(_randomGenerator));
        iceCreds->setRemoteCredentials(params["iceUfrag"].asString(),
            params["icePwd"].asString());
        UserPtr user = boost::make_shared<User>(userId, scopeId);

        LinkInfo uplink;
        uplink.peerAudioSsrc = newSsrc();
        uplink.peerVideoSsrc = newSsrc();
        uplink.streamerAudioSsrc = newSsrc();
        uplink.streamerVideoSsrc = newSsrc();
        uplink.iceCredentials = iceCreds;

        uplink.peerKeySalt = base64Decode(params["cryptoKey"].asString());
        uplink.srtpPeerSession.setKey(uplink.peerKeySalt, SRTP_SESSION_DIRECTION_INBOUND);

        uplink.streamerKeySalt = defaultSizeKeySalt(_randomGenerator);
        uplink.srtpStreamerSession.setKey(uplink.streamerKeySalt, SRTP_SESSION_DIRECTION_OUTBOUND);

        user->_uplink = uplink;
        assert(user->_uplink.iceCredentials);
        user->_uplinkOfferSdp = params["offerSdp"].asString();

        _signalingUsers.insert(std::make_pair(hdl, user));

        _udpServer->addUser(user);
        gGlobalScope.addUser(userId, user);

        result["type"] = "authResponse";
        Json::Value data;

        data["cryptoKey"] = base64Encode(uplink.streamerKeySalt);
        data["peerAudioSsrc"] = uplink.peerAudioSsrc;
        data["peerVideoSsrc"] = uplink.peerVideoSsrc;
        data["streamerAudioSsrc"] = uplink.streamerAudioSsrc;
        data["streamerVideoSsrc"] = uplink.streamerVideoSsrc;

        data["iceUfrag"] = iceCreds->localUfrag();
        data["icePwd"] = iceCreds->localPwd();
        // foundation component-id protocol priority address port type
        data["candidate"] = gServerCandidate;
        data["port"] = gServerPortStr; //< for m= lines
        data["address"] = gServerIpAddr; //< for c= lines
        data["offerSdp"] = user->_uplinkOfferSdp;

        result["data"] = data;
        sendJson(hdl, result);

        // notify other clients about current user connected
        BOOST_FOREACH(SignalingMap::value_type& userPair, _signalingUsers)
        {
            if (userPair.first.lock().get() != hdl.lock().get())
            {
                reportConnectedUser(userPair.first, user);
            }
        }

        // send info about users already present in current room to this user
        BOOST_FOREACH(SignalingMap::value_type& userPair, _signalingUsers)
        {
            if (userPair.first.lock().get() != hdl.lock().get())
                reportConnectedUser(hdl, userPair.second);
        }
    }

    void handleStartNewDownlink(connection_hdl hdl, const Json::Value& params)
    {
        Json::Value result;
        UserPtr user = getUserByConnection(hdl);
        if (!user)
        {
            LOG_E("User not authenticated");
            return;
        }

        IceCredentialsPtr iceCreds = boost::make_shared<IceCredentials>(boost::ref(_randomGenerator));
        iceCreds->setRemoteCredentials(params["iceUfrag"].asString(),
            params["icePwd"].asString());

        int senderUserId = params["userId"].asInt();
        UserPtr senderUser = gGlobalScope.getUser(senderUserId);
        assert(senderUser);

        LinkInfo downlink;
        downlink.iceCredentials = iceCreds;
        // JS SSRCs for RTCP RR and RTCP feedback packets
        downlink.peerAudioSsrc = newSsrc();
        downlink.peerVideoSsrc = newSsrc();
        // These SSRCs will be used in SDP answer from streamer.
        // We don't remap remote user's uplink SSRCs, just announce them to new
        // downlink connection.
        downlink.streamerAudioSsrc = senderUser->_uplink.peerAudioSsrc;
        downlink.streamerVideoSsrc = senderUser->_uplink.peerVideoSsrc;

        downlink.peerKeySalt = base64Decode(params["cryptoKey"].asString());
        downlink.srtpPeerSession.setKey(downlink.peerKeySalt, SRTP_SESSION_DIRECTION_INBOUND);

        downlink.streamerKeySalt = defaultSizeKeySalt(_randomGenerator);
        downlink.srtpStreamerSession.setKey(downlink.streamerKeySalt, SRTP_SESSION_DIRECTION_OUTBOUND);

        user->_downlinks[senderUserId] = downlink;
        _udpServer->addLink(iceCreds->verifyingUname(), user,
            MEDIA_LINK_TYPE_DOWNLINK, downlink.peerAudioSsrc, downlink.peerVideoSsrc,
            senderUserId, downlink.streamerVideoSsrc);

        result["type"] = "userEvent";
        Json::Value data;
        data["eventType"] = "downlinkConnectionAnswer";
        data["userId"] = senderUserId;
        data["cryptoKey"] = base64Encode(downlink.streamerKeySalt);
        LOG_D("Setting key " << base64Encode(downlink.streamerKeySalt) << " for s->p for SSRCs " << downlink.streamerAudioSsrc);

        data["iceUfrag"] = iceCreds->localUfrag();
        data["icePwd"] = iceCreds->localPwd();

        data["offerSdp"] = params["offerSdp"];
        data["answerSdp"] = senderUser->_uplinkOfferSdp;

        data["peerAudioSsrc"] = downlink.peerAudioSsrc;
        data["peerVideoSsrc"] = downlink.peerVideoSsrc;
        data["streamerAudioSsrc"] = downlink.streamerAudioSsrc;
        data["streamerVideoSsrc"] = downlink.streamerVideoSsrc;

        // foundation component-id protocol priority address port type
        data["candidate"] = gServerCandidate;
        data["port"] = gServerPortStr; //< for m= lines
        data["address"] = gServerIpAddr; //< for c= lines

        result["data"] = data;
        sendJson(hdl, result);
    }

    void onMessage(connection_hdl hdl, server::message_ptr msg)
    {
        //LOG_D("Got message: " << msg->get_payload());

        Json::Reader reader;
        Json::Value root;
        bool parsed = reader.parse(msg->get_payload(), root);
        if (!parsed)
        {
            LOG_E("Failed to parse message");
            return;
        }

        
        std::string msgType = root["type"].asString();
        Json::Value params = root["data"];

        if (msgType == "authRequest")
        {
            handleAuthRequest(hdl, params);
        }
        else if (msgType == "userEvent")
        {
            if (params["eventType"] == "startNewDownlink")
            {
                handleStartNewDownlink(hdl, params);
            }
            else
            {
                LOG_E("Unknown user event");
                assert(false);
            }
        }
        else if (msgType == "iceCandidate")
        {
            //unsigned short port = boost::lexical_cast<unsigned short>(root["data"]["port"].asString());
            //std::string ipAddr = root["data"]["ipAddr"].asString();
        }
        else
        {
            LOG_E("Unknown message type: " << msgType);
            assert(false);
        }
    }

    void sendJson(connection_hdl hdl, const Json::Value& msg)
    {
        Json::FastWriter writer;
        std::string resultMsg = writer.write(msg);
        _server.send(hdl, resultMsg, websocketpp::frame::opcode::TEXT);
    }

    unsigned newSsrc()
    {
        return ++_ssrcCounter;
    }

    UserPtr getUserByConnection(connection_hdl hdl)
    {
        SignalingMap::iterator it = _signalingUsers.find(hdl);
        if (it == _signalingUsers.end())
        {
            return UserPtr();
        }

        return it->second;
    }

    void run(uint16_t port)
    {
        _server.listen(port);
        _server.start_accept();

        try
        {
            _server.run();
        }
        catch (const std::exception & e)
        {
            std::cout << e.what() << std::endl;
        }
        catch (websocketpp::lib::error_code e)
        {
            std::cout << e.message() << std::endl;
        }
        catch (...)
        {
            std::cout << "other exception" << std::endl;
        }
    }
private:
    server _server;
    std::set<connection_hdl> _connections;
    unsigned _ssrcCounter;

    // for signaling connection mapping
    typedef std::map<connection_hdl, UserPtr> SignalingMap;
    SignalingMap _signalingUsers;
    UdpServer* _udpServer;

    boost::mt19937 _randomGenerator;
};

int main()
{
    LOG_D("starting server");

    boost::asio::io_service ioService;
    BroadcastServer server(ioService);
    UdpServer udpServer(ioService, boost::asio::ip::address_v4::from_string(gServerIpAddr));
    server.setUdpServer(&udpServer);
    udpServer.start(gServerPort);
    boost::thread thr(bind(&BroadcastServer::run, &server, gSignalingPort));
    
    thr.join();
    LOG_D("server finished working");

    return 0;
}
