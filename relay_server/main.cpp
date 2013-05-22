#include <UdpServer.hpp>
#include <User.hpp>
#include <IceCredentials.hpp>
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

class Scope
{
public:
	Scope(const std::string& scopeId): _scopeId(scopeId)
	{
		_keyAndSalt = "jJv7OTSY+x5YFIisLr59b5OVIBCrHT+5gK6OZmNd";
	}

	

//private:
	std::string _scopeId;
	std::string _keyAndSalt;
};

Scope gGlobalScope("1");


class BroadcastServer
{
public:
	BroadcastServer(): _ssrcCounter(0), _udpServer(0),
        _randomGenerator(sm_uint32_t(std::time(0)))
    {
        //_server.set_access_channels(websocketpp::log::alevel::all);
        //_server.clear_access_channels(websocketpp::log::alevel::frame_payload);
        _server.init_asio();
                
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
    
    void onMessage(connection_hdl hdl, server::message_ptr msg)
    {
        LOG_D("Got message: " << msg->get_payload());

		Json::Reader reader;
		Json::Value root;
		bool parsed = reader.parse(msg->get_payload(), root);
		if (!parsed)
		{
			LOG_E("Failed to parse message");
			return;
		}

		Json::Value result;
		std::string msgType = root["type"].asString();
        Json::Value params = root["data"];

		if (msgType == "authRequest")
		{
			int userId = params["userId"].asInt();
			std::string scopeId = params["scopeId"].asString();

            IceCredentials iceCreds(_randomGenerator);
			iceCreds.setRemoteCredentials(params["iceUfrag"].asString(),
                params["icePwd"].asString());
            UserPtr user = boost::make_shared<User>(userId, scopeId, iceCreds);

            user->_audioSsrc = newSsrc();
            user->_videoSsrc = newSsrc();

			_signalingUsers.insert(std::make_pair(hdl, user));

            _udpServer->addUser(user);
            //_udpServer->addRecognizedIceUser(iceCreds.verifyingUname(),
            //    iceCreds.verifyingPwd());

			result["type"] = "authResponse";
			Json::Value data;
			data["cryptoKey"] = gGlobalScope._keyAndSalt;
			data["audioSsrc"] = user->_audioSsrc;
			data["videoSsrc"] = user->_videoSsrc;
			data["iceUfrag"] = iceCreds.localUfrag();
			data["icePwd"] = iceCreds.localPwd();
			// foundation component-id protocol priority address port type
			data["candidate"] = "0 1 UDP 2113667327 192.168.1.33 7000 typ host";
			data["port"] = "7000"; //< for m= lines
			data["address"] = "192.168.1.33"; //< for c= lines

			result["data"] = data;
			sendJson(hdl, result);

            // notify other clients about current user connected
            BOOST_FOREACH(SignalingMap::value_type& userPair, _signalingUsers)
            {
                if (userPair.first.lock().get() != hdl.lock().get())
                    reportConnectedUser(userPair.first, user);
            }

            // send info about users already present in current room to this user
            //BOOST_FOREACH(SignalingMap::value_type& userPair, _signalingUsers)
            //{
            //    if (userPair.first.lock().get() != hdl.lock().get())
            //        reportConnectedUser(hdl, userPair.second);
            //}
		}
        else if (msgType == "userEvent")
        {
            if (params["eventType"] == "startNewDownlink")
            {
                UserPtr user = getUserByConnection(hdl);
                if (!user)
                {
                    LOG_E("User not authenticated");
                    return;
                }

                IceCredentialsPtr iceCreds = boost::make_shared<IceCredentials>(boost::ref(_randomGenerator));
			    iceCreds->setRemoteCredentials(params["iceUfrag"].asString(),
                    params["icePwd"].asString());
                int userId = params["userId"].asInt();

                user->_downlinkIceCredentials[userId] = iceCreds;
                _udpServer->addRecognizedIceUser(iceCreds->verifyingUname(),
                    iceCreds->verifyingPwd());

                // TODO: save and set in JS SSRCs for RTCP RR and feedback packets
			    unsigned audioSsrc = newSsrc();
			    unsigned videoSsrc = newSsrc();
			    //_ssrcUsers.insert(std::make_pair(audioSsrc, user));
			    //_ssrcUsers.insert(std::make_pair(videoSsrc, user));

			    result["type"] = "userEvent";
			    Json::Value data;
                data["eventType"] = "downlinkConnectionAnswer";
                data["userId"] = userId;
			    data["cryptoKey"] = gGlobalScope._keyAndSalt;
			    data["audioSsrc"] = audioSsrc;
			    data["videoSsrc"] = videoSsrc;
			    data["iceUfrag"] = iceCreds->localUfrag();
			    data["icePwd"] = iceCreds->localPwd();
			    // foundation component-id protocol priority address port type
			    data["candidate"] = "0 1 UDP 2113667327 192.168.1.33 7000 typ host";
			    data["port"] = "7000"; //< for m= lines
			    data["address"] = "192.168.1.33"; //< for c= lines

			    result["data"] = data;
			    sendJson(hdl, result);
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
    BroadcastServer server;
    UdpServer udpServer(boost::asio::ip::address_v4::from_string("192.168.1.33"));
    server.setUdpServer(&udpServer);
    boost::thread thr(bind(&BroadcastServer::run, &server, 10000));
    udpServer.start(7000);
    
    thr.join();
    LOG_D("server finished working");

    return 0;
}